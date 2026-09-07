#!/usr/bin/env python3
"""Generate a three-piece, low-poly orca shell without external CAD packages.

The generator intentionally uses only the Python standard library.  Geometry is
stored in millimetres and exported as triangulated FACETED_BREP STEP, binary STL
and OBJ.  A separate Windows script imports the STEP bodies into SOLIDWORKS 2025
and writes the native SLDPRT/SLDASM files.

Coordinate system
-----------------
X: nose (0 mm) to tail (420 mm)
Y: port/starboard
Z: ventral/dorsal
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence


Vec3 = tuple[float, float, float]
Tri = tuple[int, int, int]


WALL_MM = 1.5
FACETS = 12
FIT_RADIAL_CLEARANCE_MM = 0.30
FIT_TOLERANCE_MM = 0.10
ASSEMBLY_HOLE_DIAMETER_MM = 3.40


@dataclass
class Mesh:
    name: str
    vertices: list[Vec3] = field(default_factory=list)
    faces: list[Tri] = field(default_factory=list)

    def vertex(self, point: Vec3) -> int:
        self.vertices.append(tuple(float(v) for v in point))
        return len(self.vertices) - 1

    def triangle(self, a: int, b: int, c: int, target: Vec3 | None = None) -> None:
        if target is not None:
            pa, pb, pc = self.vertices[a], self.vertices[b], self.vertices[c]
            normal = cross(sub(pb, pa), sub(pc, pa))
            if dot(normal, target) < 0.0:
                b, c = c, b
        self.faces.append((a, b, c))


@dataclass(frozen=True)
class Validation:
    name: str
    vertices: int
    triangles: int
    boundary_edges: int
    nonmanifold_edges: int
    orientation_mismatched_edges: int
    degenerate_triangles: int
    signed_volume_mm3: float
    bbox_min_mm: Vec3
    bbox_max_mm: Vec3

    @property
    def watertight(self) -> bool:
        return (
            self.boundary_edges == 0
            and self.nonmanifold_edges == 0
            and self.orientation_mismatched_edges == 0
            and self.degenerate_triangles == 0
        )


@dataclass
class Part:
    name: str
    solids: list[Mesh]


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def norm(a: Vec3) -> float:
    return math.sqrt(dot(a, a))


def add_quad(
    mesh: Mesh,
    a: int,
    b: int,
    c: int,
    d: int,
    target: Vec3,
    flip_diagonal: bool,
) -> None:
    """Triangulate a quad and orient both triangles toward target."""
    if flip_diagonal:
        mesh.triangle(a, b, d, target)
        mesh.triangle(b, c, d, target)
    else:
        mesh.triangle(a, b, c, target)
        mesh.triangle(a, c, d, target)


def profile_parameters(profile: Sequence[float]) -> tuple[float, float, float, float, float]:
    """Return x, half-width, half-height, Z centre and superellipse power.

    Three-value profiles remain backwards compatible with the first revision.
    The extra values let the production shell use a raised crown and fuller
    low-poly shoulders without changing the validated offset algorithm.
    """
    if len(profile) == 3:
        x, ry, rz = profile
        return float(x), float(ry), float(rz), 0.0, 2.0
    if len(profile) == 4:
        x, ry, rz, z_center = profile
        return float(x), float(ry), float(rz), float(z_center), 2.0
    if len(profile) == 5:
        x, ry, rz, z_center, power = profile
        if power < 2.0:
            raise ValueError("Superellipse power must be at least 2.0")
        return float(x), float(ry), float(rz), float(z_center), float(power)
    raise ValueError(f"Profile must have 3, 4 or 5 values, got {profile!r}")


def superellipse_point(
    x: float,
    ry: float,
    rz: float,
    z_center: float,
    power: float,
    theta: float,
) -> Vec3:
    cosine = math.cos(theta)
    sine = math.sin(theta)
    exponent = 2.0 / power
    y_scale = math.copysign(abs(cosine) ** exponent, cosine)
    z_scale = math.copysign(abs(sine) ** exponent, sine)
    return (x, ry * y_scale, z_center + rz * z_scale)


def ellipse_ring(
    mesh: Mesh,
    x: float,
    ry: float,
    rz: float,
    count: int = FACETS,
    theta_offset: float = 0.0,
    z_center: float = 0.0,
    power: float = 2.0,
) -> list[int]:
    result: list[int] = []
    for j in range(count):
        theta = theta_offset + (2.0 * math.pi * j / count)
        result.append(mesh.vertex(superellipse_point(x, ry, rz, z_center, power, theta)))
    return result


def profile_gap_stats(
    profile_a: Sequence[float],
    profile_b: Sequence[float],
    samples: int = 720,
) -> dict[str, float]:
    """Sample corresponding superellipse points for a locating-lip gap check."""
    a = profile_parameters(profile_a)
    b = profile_parameters(profile_b)
    distances = []
    for index in range(samples):
        theta = 2.0 * math.pi * index / samples
        distances.append(math.dist(superellipse_point(*a, theta), superellipse_point(*b, theta)))
    return {"min_mm": min(distances), "max_mm": max(distances)}


def profile_polygon_gap_stats(
    female_profile: Sequence[float],
    male_profile: Sequence[float],
    count: int = FACETS,
    samples_per_edge: int = 64,
) -> dict[str, float]:
    """Measure the nearest gap on the actual faceted locating-lip polygons."""
    female = profile_parameters(female_profile)
    male = profile_parameters(male_profile)
    female_points = [
        superellipse_point(*female, 2.0 * math.pi * index / count)
        for index in range(count)
    ]
    male_points = [
        superellipse_point(*male, 2.0 * math.pi * index / count)
        for index in range(count)
    ]

    def point_segment_distance(point: Vec3, start: Vec3, end: Vec3) -> float:
        direction = sub(end, start)
        length_squared = dot(direction, direction)
        if length_squared < 1e-12:
            return math.dist(point, start)
        parameter = max(0.0, min(1.0, dot(sub(point, start), direction) / length_squared))
        closest = (
            start[0] + parameter * direction[0],
            start[1] + parameter * direction[1],
            start[2] + parameter * direction[2],
        )
        return math.dist(point, closest)

    gaps = []
    for edge_index in range(count):
        start = male_points[edge_index]
        end = male_points[(edge_index + 1) % count]
        for sample_index in range(samples_per_edge + 1):
            parameter = sample_index / samples_per_edge
            point = (
                start[0] + parameter * (end[0] - start[0]),
                start[1] + parameter * (end[1] - start[1]),
                start[2] + parameter * (end[2] - start[2]),
            )
            gaps.append(
                min(
                    point_segment_distance(
                        point,
                        female_points[female_edge],
                        female_points[(female_edge + 1) % count],
                    )
                    for female_edge in range(count)
                )
            )
    return {"min_mm": min(gaps), "max_mm": max(gaps)}


def mesh_plane_section(
    mesh: Mesh,
    faces: Sequence[Tri],
    x_mm: float,
    *,
    tolerance: float = 1e-8,
) -> list[Vec3]:
    """Intersect a triangulated side surface with an X-normal inspection plane."""
    candidates: list[Vec3] = []
    for face in faces:
        triangle = [mesh.vertices[index] for index in face]
        for start, end in zip(triangle, triangle[1:] + triangle[:1]):
            start_delta = start[0] - x_mm
            end_delta = end[0] - x_mm
            if abs(start_delta) <= tolerance:
                candidates.append((x_mm, start[1], start[2]))
            if start_delta * end_delta < -(tolerance * tolerance):
                parameter = (x_mm - start[0]) / (end[0] - start[0])
                candidates.append(
                    (
                        x_mm,
                        start[1] + parameter * (end[1] - start[1]),
                        start[2] + parameter * (end[2] - start[2]),
                    )
                )

    unique: list[Vec3] = []
    for point in candidates:
        if not any(math.dist(point, existing) <= tolerance for existing in unique):
            unique.append(point)
    if len(unique) < 3:
        raise ValueError(f"{mesh.name}: X={x_mm} mm section has only {len(unique)} points")
    center_y = sum(point[1] for point in unique) / len(unique)
    center_z = sum(point[2] for point in unique) / len(unique)
    return sorted(
        unique,
        key=lambda point: math.atan2(point[2] - center_z, point[1] - center_y),
    )


def polygon_clearance_stats(
    female_points: Sequence[Vec3],
    male_points: Sequence[Vec3],
    *,
    samples_per_edge: int = 64,
) -> dict[str, float | int | bool]:
    """Measure an actual faceted male/female section, including containment."""

    def yz(point: Vec3) -> tuple[float, float]:
        return point[1], point[2]

    def cross2(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def on_segment(
        point: tuple[float, float],
        start: tuple[float, float],
        end: tuple[float, float],
        tolerance: float = 1e-9,
    ) -> bool:
        return (
            abs(cross2(start, end, point)) <= tolerance
            and min(start[0], end[0]) - tolerance <= point[0] <= max(start[0], end[0]) + tolerance
            and min(start[1], end[1]) - tolerance <= point[1] <= max(start[1], end[1]) + tolerance
        )

    def segments_intersect(
        a: tuple[float, float],
        b: tuple[float, float],
        c: tuple[float, float],
        d: tuple[float, float],
    ) -> bool:
        ab_c, ab_d = cross2(a, b, c), cross2(a, b, d)
        cd_a, cd_b = cross2(c, d, a), cross2(c, d, b)
        if ((ab_c > 0) != (ab_d > 0)) and ((cd_a > 0) != (cd_b > 0)):
            return True
        return (
            on_segment(c, a, b)
            or on_segment(d, a, b)
            or on_segment(a, c, d)
            or on_segment(b, c, d)
        )

    def point_inside(point: tuple[float, float], polygon: Sequence[Vec3]) -> bool:
        inside = False
        px, py = point
        polygon_yz = [yz(item) for item in polygon]
        for start, end in zip(polygon_yz, polygon_yz[1:] + polygon_yz[:1]):
            if on_segment(point, start, end):
                return True
            if (start[1] > py) != (end[1] > py):
                crossing_x = start[0] + (py - start[1]) * (end[0] - start[0]) / (end[1] - start[1])
                if crossing_x > px:
                    inside = not inside
        return inside

    def point_segment_distance_yz(point: Vec3, start: Vec3, end: Vec3) -> float:
        p, a, b = yz(point), yz(start), yz(end)
        direction = (b[0] - a[0], b[1] - a[1])
        length_squared = direction[0] ** 2 + direction[1] ** 2
        if length_squared < 1e-12:
            return math.dist(p, a)
        parameter = max(
            0.0,
            min(
                1.0,
                ((p[0] - a[0]) * direction[0] + (p[1] - a[1]) * direction[1])
                / length_squared,
            ),
        )
        closest = (a[0] + parameter * direction[0], a[1] + parameter * direction[1])
        return math.dist(p, closest)

    female_edges = list(zip(female_points, female_points[1:] + female_points[:1]))
    male_edges = list(zip(male_points, male_points[1:] + male_points[:1]))
    intersects = any(
        segments_intersect(yz(male_start), yz(male_end), yz(female_start), yz(female_end))
        for male_start, male_end in male_edges
        for female_start, female_end in female_edges
    )
    male_inside = all(point_inside(yz(point), female_points) for point in male_points)

    gaps: list[float] = []
    for source_edges, target_edges in ((male_edges, female_edges), (female_edges, male_edges)):
        for start, end in source_edges:
            for sample_index in range(samples_per_edge + 1):
                parameter = sample_index / samples_per_edge
                point = (
                    start[0] + parameter * (end[0] - start[0]),
                    start[1] + parameter * (end[1] - start[1]),
                    start[2] + parameter * (end[2] - start[2]),
                )
                gaps.append(
                    min(
                        point_segment_distance_yz(point, target_start, target_end)
                        for target_start, target_end in target_edges
                    )
                )
    return {
        "min_mm": min(gaps),
        "max_mm": max(gaps),
        "female_section_vertices": len(female_points),
        "male_section_vertices": len(male_points),
        "male_inside_female": male_inside,
        "boundary_intersection": intersects,
    }


def mesh_joint_clearance_stats(
    female_mesh: Mesh,
    female_ring_count: int,
    male_mesh: Mesh,
    male_ring_count: int,
    x_mm: float,
) -> dict[str, float | int | bool]:
    female_side_triangles = (female_ring_count - 1) * FACETS * 2
    male_side_triangles = (male_ring_count - 1) * FACETS * 2
    female_inner_faces = female_mesh.faces[female_side_triangles : 2 * female_side_triangles]
    male_outer_faces = male_mesh.faces[:male_side_triangles]
    female_section = mesh_plane_section(female_mesh, female_inner_faces, x_mm)
    male_section = mesh_plane_section(male_mesh, male_outer_faces, x_mm)
    return polygon_clearance_stats(female_section, male_section)


def hollow_segment(
    name: str,
    outer_profiles: Sequence[Sequence[float]],
    *,
    wall_mm: float = WALL_MM,
    closed_start: bool,
    closed_end: bool,
    count: int = FACETS,
    normal_offset: bool = True,
) -> Mesh:
    """Build one watertight hollow/tubular segment from elliptical rings."""
    if len(outer_profiles) < 2:
        raise ValueError("A segment needs at least two profiles")
    mesh = Mesh(name)
    outer_rings: list[list[int]] = []
    inner_rings: list[list[int]] = []

    for i, profile in enumerate(outer_profiles):
        x, ry, rz, z_center, power = profile_parameters(profile)
        if min(ry, rz) <= wall_mm:
            raise ValueError(f"{name}: profile {i} is smaller than the wall")
        outer_rings.append(
            ellipse_ring(
                mesh,
                x,
                ry,
                rz,
                count,
                z_center=z_center,
                power=power,
            )
        )

    # Outer skin; target is the radial direction away from the X axis.
    outer_face_start = len(mesh.faces)
    for i in range(len(outer_rings) - 1):
        for j in range(count):
            k = (j + 1) % count
            ids = (
                outer_rings[i][j],
                outer_rings[i + 1][j],
                outer_rings[i + 1][k],
                outer_rings[i][k],
            )
            points = [mesh.vertices[q] for q in ids]
            target = (0.0, sum(p[1] for p in points), sum(p[2] for p in points))
            add_quad(mesh, *ids, target, flip_diagonal=((i + j) % 2 == 1))

    if normal_offset:
        # Offset along area-weighted facet normals.  The distance is corrected
        # by the smallest adjacent normal cosine so every adjacent outer plane
        # is at least wall_mm away.  At open split planes the displacement is
        # projected into YZ, keeping the mating ring exactly on its X station.
        incident_normals: dict[int, list[Vec3]] = defaultdict(list)
        area_sums: dict[int, Vec3] = defaultdict(lambda: (0.0, 0.0, 0.0))
        for face in mesh.faces[outer_face_start:]:
            pa, pb, pc = (mesh.vertices[index] for index in face)
            raw = cross(sub(pb, pa), sub(pc, pa))
            raw_length = norm(raw)
            unit = (raw[0] / raw_length, raw[1] / raw_length, raw[2] / raw_length)
            for index in face:
                incident_normals[index].append(unit)
                old = area_sums[index]
                area_sums[index] = (old[0] + raw[0], old[1] + raw[1], old[2] + raw[2])

        for i, ring in enumerate(outer_rings):
            inner_ring = []
            open_boundary = (i == 0 and not closed_start) or (
                i == len(outer_rings) - 1 and not closed_end
            )
            for index in ring:
                summed = area_sums[index]
                if open_boundary:
                    summed = (0.0, summed[1], summed[2])
                summed_length = norm(summed)
                if summed_length < 1e-12:
                    raise ValueError(f"{name}: zero vertex normal at {index}")
                direction = (
                    summed[0] / summed_length,
                    summed[1] / summed_length,
                    summed[2] / summed_length,
                )
                cosines = [dot(direction, face_normal) for face_normal in incident_normals[index]]
                minimum_cosine = min(c for c in cosines if c > 1e-6)
                distance = wall_mm / minimum_cosine
                outer_point = mesh.vertices[index]
                inner_x = outer_point[0] - distance * direction[0]
                if i == 0 and closed_start:
                    inner_x = outer_point[0] + wall_mm
                elif i == len(outer_rings) - 1 and closed_end:
                    inner_x = outer_point[0] - wall_mm
                inner_ring.append(
                    mesh.vertex(
                        (
                            inner_x,
                            outer_point[1] - distance * direction[1],
                            outer_point[2] - distance * direction[2],
                        )
                    )
                )
            inner_rings.append(inner_ring)
    else:
        for i, profile in enumerate(outer_profiles):
            x, ry, rz, z_center, power = profile_parameters(profile)
            inner_x = x
            if i == 0 and closed_start:
                inner_x += wall_mm
            if i == len(outer_profiles) - 1 and closed_end:
                inner_x -= wall_mm
            inner_rings.append(
                ellipse_ring(
                    mesh,
                    inner_x,
                    ry - wall_mm,
                    rz - wall_mm,
                    count,
                    z_center=z_center,
                    power=power,
                )
            )

    # Inner skin; its outward normal points into the cavity.
    for i in range(len(inner_rings) - 1):
        for j in range(count):
            k = (j + 1) % count
            ids = (
                inner_rings[i][j],
                inner_rings[i][k],
                inner_rings[i + 1][k],
                inner_rings[i + 1][j],
            )
            points = [mesh.vertices[q] for q in ids]
            target = (0.0, -sum(p[1] for p in points), -sum(p[2] for p in points))
            add_quad(mesh, *ids, target, flip_diagonal=((i + j) % 2 == 0))

    def annular_end(outer: list[int], inner: list[int], target_x: float) -> None:
        for j in range(count):
            k = (j + 1) % count
            add_quad(
                mesh,
                outer[j],
                outer[k],
                inner[k],
                inner[j],
                (target_x, 0.0, 0.0),
                flip_diagonal=(j % 2 == 1),
            )

    def disk(ring: list[int], target_x: float) -> None:
        p = [mesh.vertices[q] for q in ring]
        center = mesh.vertex(
            (
                sum(q[0] for q in p) / len(p),
                sum(q[1] for q in p) / len(p),
                sum(q[2] for q in p) / len(p),
            )
        )
        for j in range(count):
            mesh.triangle(center, ring[j], ring[(j + 1) % count], (target_x, 0.0, 0.0))

    if closed_start:
        disk(outer_rings[0], -1.0)
        disk(inner_rings[0], +1.0)
    else:
        annular_end(outer_rings[0], inner_rings[0], -1.0)

    if closed_end:
        disk(outer_rings[-1], +1.0)
        disk(inner_rings[-1], -1.0)
    else:
        annular_end(outer_rings[-1], inner_rings[-1], +1.0)
    return mesh


def annular_tube(
    name: str,
    outer_profiles: Sequence[Sequence[float]],
    thickness_mm: float,
    count: int = FACETS,
    *,
    normal_offset: bool = True,
) -> Mesh:
    return hollow_segment(
        name,
        outer_profiles,
        wall_mm=thickness_mm,
        closed_start=False,
        closed_end=False,
        count=count,
        normal_offset=normal_offset,
    )


def axial_hole_boss(
    name: str,
    x0: float,
    x1: float,
    y: float,
    z: float,
    outer_radius: float = 4.0,
    hole_radius: float = ASSEMBLY_HOLE_DIAMETER_MM / 2.0,
    count: int = 16,
) -> Mesh:
    """Closed annular cylinder along X; its bore is the assembly hole."""
    mesh = Mesh(name)
    outer = []
    inner = []
    for x in (x0, x1):
        outer.append(
            [
                mesh.vertex(
                    (
                        x,
                        y + outer_radius * math.cos(2 * math.pi * j / count),
                        z + outer_radius * math.sin(2 * math.pi * j / count),
                    )
                )
                for j in range(count)
            ]
        )
        inner.append(
            [
                mesh.vertex(
                    (
                        x,
                        y + hole_radius * math.cos(2 * math.pi * j / count),
                        z + hole_radius * math.sin(2 * math.pi * j / count),
                    )
                )
                for j in range(count)
            ]
        )

    for j in range(count):
        k = (j + 1) % count
        theta = 2 * math.pi * (j + 0.5) / count
        outward = (0.0, math.cos(theta), math.sin(theta))
        inward = (0.0, -math.cos(theta), -math.sin(theta))
        add_quad(mesh, outer[0][j], outer[1][j], outer[1][k], outer[0][k], outward, j % 2 == 0)
        add_quad(mesh, inner[0][j], inner[0][k], inner[1][k], inner[1][j], inward, j % 2 == 1)
        add_quad(mesh, outer[0][j], outer[0][k], inner[0][k], inner[0][j], (-1, 0, 0), j % 2 == 0)
        add_quad(mesh, outer[1][j], inner[1][j], inner[1][k], outer[1][k], (+1, 0, 0), j % 2 == 1)
    return mesh


def convex_polyhedron(name: str, points: Sequence[Vec3], polygons: Sequence[Sequence[int]]) -> Mesh:
    mesh = Mesh(name, vertices=[tuple(map(float, p)) for p in points])
    centroid = (
        sum(p[0] for p in mesh.vertices) / len(mesh.vertices),
        sum(p[1] for p in mesh.vertices) / len(mesh.vertices),
        sum(p[2] for p in mesh.vertices) / len(mesh.vertices),
    )
    for polygon in polygons:
        if len(polygon) < 3:
            continue
        face_center = tuple(
            sum(mesh.vertices[i][axis] for i in polygon) / len(polygon)
            for axis in range(3)
        )
        target = sub(face_center, centroid)
        for j in range(1, len(polygon) - 1):
            mesh.triangle(polygon[0], polygon[j], polygon[j + 1], target)
    return mesh


def triangular_prism(name: str, triangle_a: Sequence[Vec3], triangle_b: Sequence[Vec3]) -> Mesh:
    points = list(triangle_a) + list(triangle_b)
    polygons = [
        (0, 1, 2),
        (3, 5, 4),
        (0, 3, 4, 1),
        (1, 4, 5, 2),
        (2, 5, 3, 0),
    ]
    return convex_polyhedron(name, points, polygons)


def quadrilateral_prism(
    name: str,
    face_a: Sequence[Vec3],
    face_b: Sequence[Vec3],
) -> Mesh:
    """Create a closed four-sided plate/wedge with triangulated faces."""
    if len(face_a) != 4 or len(face_b) != 4:
        raise ValueError("quadrilateral_prism requires two four-point faces")
    points = list(face_a) + list(face_b)
    polygons = [
        (0, 1, 2, 3),
        (4, 7, 6, 5),
        (0, 4, 5, 1),
        (1, 5, 6, 2),
        (2, 6, 7, 3),
        (3, 7, 4, 0),
    ]
    return convex_polyhedron(name, points, polygons)


def rectangular_prism(
    name: str,
    x0: float,
    x1: float,
    y0: float,
    y1: float,
    z0: float,
    z1: float,
) -> Mesh:
    lower = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)]
    upper = [(x, y, z1) for x, y, _ in lower]
    return quadrilateral_prism(name, lower, upper)


def side_strake(
    name: str,
    x0: float,
    x1: float,
    y_inner0: float,
    y_inner1: float,
    y_outer0: float,
    y_outer1: float,
    z0: float,
    z1: float,
) -> Mesh:
    """Tapered raised side rib used as a non-penetrating intake/vent cue."""
    inner = [
        (x0, y_inner0, z0),
        (x1, y_inner1, z0),
        (x1, y_inner1, z1),
        (x0, y_inner0, z1),
    ]
    outer = [
        (x0, y_outer0, z0),
        (x1, y_outer1, z0),
        (x1, y_outer1, z1),
        (x0, y_outer0, z1),
    ]
    return quadrilateral_prism(name, inner, outer)


def tapered_rib(
    name: str,
    x0: float,
    x1: float,
    y_center: float,
    side_sign: float,
    anchor_y0: float,
    anchor_y1: float,
    z0: float,
    z1: float,
) -> Mesh:
    """A tapered box rib that joins an axial boss to the surrounding shell."""
    near_y = y_center + side_sign * 2.5
    far_y0 = side_sign * anchor_y0
    far_y1 = side_sign * anchor_y1
    points = [
        (x0, near_y, z0),
        (x0, far_y0, z0),
        (x0, far_y0, z1),
        (x0, near_y, z1),
        (x1, near_y, z0),
        (x1, far_y1, z0),
        (x1, far_y1, z1),
        (x1, near_y, z1),
    ]
    polygons = [
        (0, 3, 2, 1),
        (4, 5, 6, 7),
        (0, 1, 5, 4),
        (3, 7, 6, 2),
        (0, 4, 7, 3),
        (1, 2, 6, 5),
    ]
    return convex_polyhedron(name, points, polygons)


def supported_dowel_pair(
    prefix: str,
    x0: float,
    x1: float,
    center_y: float,
    anchor_y0: float,
    anchor_y1: float,
    center_z: float = 0.0,
) -> list[Mesh]:
    """Two axial annular bosses plus paired ribs that do not obstruct the bore."""
    solids: list[Mesh] = []
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        center = sign * center_y
        solids.append(
            axial_hole_boss(
                f"{prefix}_Dowel_{label}",
                x0,
                x1,
                center,
                center_z,
            )
        )
        for rib_label, z0, z1 in (("Upper", 2.4, 4.0), ("Lower", -4.0, -2.4)):
            solids.append(
                tapered_rib(
                    f"{prefix}_{label}_{rib_label}_Rib",
                    x0,
                    x1,
                    center,
                    sign,
                    anchor_y0,
                    anchor_y1,
                    center_z + z0,
                    center_z + z1,
                )
            )
    return solids


def make_parts() -> dict[str, Part]:
    head_shell = hollow_segment(
        "Head_Shell_1p5mm",
        [
            (0, 10, 13, 1.0, 2.0),
            (8, 28, 28, 1.5, 2.05),
            (32, 55, 48, 3.0, 2.2),
            (65, 76, 66, 7.0, 2.4),
            (100, 87, 75, 11.0, 2.5),
            (120, 89, 77, 13.0, 2.4),
        ],
        closed_start=True,
        closed_end=False,
    )
    head_collar = annular_tube(
        "Joint120_Male_Collar",
        [
            (118.8, 88.0, 76.0, 13.0, 2.4),
            (120.0, 85.4, 73.4, 13.0, 2.4),
            (128.0, 85.4, 73.4, 13.0, 2.4),
        ],
        1.56,
    )
    head_bosses = supported_dowel_pair(
        "Joint120_Head", 110.0, 120.0, 65.0, 85.5, 88.4, center_z=13.0
    )
    head_intake_strakes: list[Mesh] = []
    head_intake_panels: list[Mesh] = []
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        panel_inner = [
            (45.0, sign * 55.0, -18.0),
            (108.0, sign * 76.0, -30.0),
            (108.0, sign * 83.0, 30.0),
            (45.0, sign * 60.0, 18.0),
        ]
        panel_outer = [
            (x, y + sign * 7.0, z)
            for x, y, z in panel_inner
        ]
        head_intake_panels.append(
            quadrilateral_prism(
                f"Head_Intake_Panel_{label}",
                panel_inner,
                panel_outer,
            )
        )
        for index, z_center in enumerate((-12.0, 0.0, 12.0), start=1):
            head_intake_strakes.append(
                side_strake(
                    f"Head_Intake_Louver_{label}_{index}",
                    48.0,
                    102.0,
                    sign * 55.0,
                    sign * 80.0,
                    sign * 67.0,
                    sign * 90.0,
                    z_center - 2.1,
                    z_center + 2.1,
                )
            )

    head_seam_tabs: list[Mesh] = []
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        y0, y1 = ((68.0, 76.0) if sign > 0 else (-76.0, -68.0))
        head_seam_tabs.append(
            rectangular_prism(
                f"Joint120_Head_Seam_Tab_{label}",
                111.0,
                118.8,
                y0,
                y1,
                58.0,
                66.0,
            )
        )

    body_shell = hollow_segment(
        "Body_Shell_1p5mm",
        [
            (120, 89, 77, 13.0, 2.4),
            (150, 90, 79, 15.0, 2.6),
            (190, 90, 79.5, 16.5, 2.7),
            (230, 84, 74, 14.0, 2.6),
            (265, 72, 64, 10.0, 2.4),
            (300, 60, 55.5, 8.5, 2.2),
        ],
        closed_start=False,
        closed_end=False,
    )
    body_front_socket = annular_tube(
        "Joint120_Female_Socket",
        [
            (120.0, 88.1, 76.1, 13.0, 2.4),
            (130.0, 88.1, 76.1, 13.0, 2.4),
        ],
        2.4,
        normal_offset=False,
    )
    body_rear_collar = annular_tube(
        "Joint300_Male_Collar",
        [
            (298.8, 59.2, 54.7, 8.5, 2.2),
            (300.0, 56.9, 52.4, 8.5, 2.2),
            (308.0, 55.46, 50.16, 8.26, 2.2),
        ],
        1.56,
    )
    body_bosses = supported_dowel_pair(
        "Joint120_Body", 128.2, 138.2, 65.0, 87.5, 88.7, center_z=13.0
    ) + supported_dowel_pair(
        "Joint300_Body", 290.0, 300.0, 42.0, 63.0, 59.2, center_z=8.5
    )

    body_seam_tabs: list[Mesh] = []
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        rear_y0, rear_y1 = ((40.0, 48.0) if sign > 0 else (-48.0, -40.0))
        body_seam_tabs.append(
            rectangular_prism(
                f"Joint300_Body_Seam_Tab_{label}",
                291.0,
                298.8,
                rear_y0,
                rear_y1,
                38.0,
                46.0,
            )
        )

    dorsal = convex_polyhedron(
        "Dorsal_Fin",
        [
            (230, -6.0, 80),
            (290, -6.0, 56),
            (257, -2.0, 122),
            (230, 6.0, 80),
            (290, 6.0, 56),
            (257, 2.0, 122),
        ],
        [(0, 1, 2), (3, 5, 4), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)],
    )
    pectorals: list[Mesh] = []
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        upper = [
            (175, sign * 76, -18),
            (235, sign * 68, -22),
            (248, sign * 109, -31),
            (224, sign * 122, -28),
        ]
        lower = [
            (upper[0][0], upper[0][1], upper[0][2] - 6.0),
            (upper[1][0], upper[1][1], upper[1][2] - 6.0),
            (upper[2][0], upper[2][1], upper[2][2] - 3.0),
            (upper[3][0], upper[3][1], upper[3][2] - 3.0),
        ]
        pectorals.append(quadrilateral_prism(f"Pectoral_{label}", lower, upper))

    hatch_panels: list[Mesh] = []
    hatch_stations = [
        (138.0, 28.0, 94.9, 93.2),
        (155.0, 44.0, 96.8, 91.8),
        (205.0, 42.0, 95.5, 91.2),
        (225.0, 24.0, 91.5, 90.4),
    ]
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        for segment_index, (start, end) in enumerate(
            zip(hatch_stations, hatch_stations[1:]),
            start=1,
        ):
            x0, width0, ridge0, outer0 = start
            x1, width1, ridge1, outer1 = end
            upper = [
                (x0, 0.0, ridge0),
                (x0, sign * width0, outer0),
                (x1, sign * width1, outer1),
                (x1, 0.0, ridge1),
            ]
            lower = [(x, y, z - 3.5) for x, y, z in upper]
            hatch_panels.append(
                quadrilateral_prism(
                    f"Top_Hatch_{label}_{segment_index}",
                    lower,
                    upper,
                )
            )

    hatch_latches: list[Mesh] = []
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        if sign > 0:
            y0, y1 = 34.0, 44.0
        else:
            y0, y1 = -44.0, -34.0
        hatch_latches.extend(
            [
                rectangular_prism(
                    f"Hatch_Latch_Forward_{label}",
                    155.0,
                    167.0,
                    y0,
                    y1,
                    91.5,
                    95.0,
                ),
                rectangular_prism(
                    f"Hatch_Latch_Aft_{label}",
                    198.0,
                    211.0,
                    y0,
                    y1,
                    90.8,
                    94.0,
                ),
            ]
        )

    tail_shell = hollow_segment(
        "Tail_Shell_1p5mm",
        [
            (300, 60, 55.5, 8.5, 2.2),
            (325, 59, 51.5, 7.5, 2.2),
            (350, 57, 47, 6.0, 2.2),
            (380, 55, 43, 4.5, 2.1),
            (405, 52, 40, 3.0, 2.0),
            (412, 50, 38, 2.0, 2.0),
        ],
        closed_start=False,
        closed_end=True,
    )
    tail_front_socket = annular_tube(
        "Joint300_Female_Socket",
        [
            (300.0, 59.0, 54.5, 8.5, 2.2),
            (310.0, 57.2, 51.7, 8.2, 2.2),
        ],
        1.8,
        normal_offset=False,
    )
    tail_bosses = supported_dowel_pair(
        "Joint300_Tail", 308.2, 318.2, 42.0, 57.5, 58.5, center_z=8.5
    )

    tail_vent_strakes: list[Mesh] = []
    tail_vent_panels: list[Mesh] = []
    tail_vent_levels = [
        (-20.0, -17.0, 51.0, 46.0),
        (-2.0, 1.0, 56.0, 51.0),
        (16.0, 19.0, 55.0, 49.0),
    ]
    for label, sign in (("Port", 1.0), ("Starboard", -1.0)):
        panel_inner = [
            (322.0, sign * 44.0, -26.0),
            (392.0, sign * 32.0, -26.0),
            (392.0, sign * 41.0, 24.0),
            (322.0, sign * 52.0, 24.0),
        ]
        panel_outer = [
            (x, y + sign * 7.0, z)
            for x, y, z in panel_inner
        ]
        tail_vent_panels.append(
            quadrilateral_prism(
                f"Tail_Vent_Panel_{label}",
                panel_inner,
                panel_outer,
            )
        )
        for index, (z0, z1, inner0, inner1) in enumerate(tail_vent_levels, start=1):
            tail_vent_strakes.append(
                side_strake(
                    f"Tail_Vent_Strake_{label}_{index}",
                    325.0,
                    388.0,
                    sign * inner0,
                    sign * inner1,
                    sign * (inner0 + 4.0),
                    sign * (inner1 + 4.0),
                    z0,
                    z1,
                )
            )

    stern_nozzles = [
        axial_hole_boss(
            f"Stern_Nozzle_{label}",
            411.0,
            420.0,
            sign * 16.0,
            -8.0,
            outer_radius=11.0,
            hole_radius=7.0,
            count=12,
        )
        for label, sign in (("Port", 1.0), ("Starboard", -1.0))
    ]
    stern_nozzle_backplate = rectangular_prism(
        "Stern_Nozzle_Backplate",
        410.5,
        413.0,
        -30.0,
        30.0,
        -22.0,
        8.0,
    )

    return {
        "Head": Part(
            "Head",
            [
                head_shell,
                head_collar,
                *head_bosses,
                *head_intake_panels,
                *head_intake_strakes,
                *head_seam_tabs,
            ],
        ),
        "Body": Part(
            "Body",
            [
                body_shell,
                body_front_socket,
                body_rear_collar,
                *body_bosses,
                *body_seam_tabs,
                dorsal,
                *pectorals,
                *hatch_panels,
                *hatch_latches,
            ],
        ),
        "Tail": Part(
            "Tail",
            [
                tail_shell,
                tail_front_socket,
                *tail_bosses,
                *tail_vent_panels,
                *tail_vent_strakes,
                stern_nozzle_backplate,
                *stern_nozzles,
            ],
        ),
    }


def validate_mesh(mesh: Mesh) -> Validation:
    edge_counts: Counter[tuple[int, int]] = Counter()
    directed: Counter[tuple[int, int]] = Counter()
    degenerate = 0
    volume6 = 0.0
    for a, b, c in mesh.faces:
        pa, pb, pc = mesh.vertices[a], mesh.vertices[b], mesh.vertices[c]
        area2 = norm(cross(sub(pb, pa), sub(pc, pa)))
        if area2 < 1e-8:
            degenerate += 1
        volume6 += dot(pa, cross(pb, pc))
        for u, v in ((a, b), (b, c), (c, a)):
            edge_counts[tuple(sorted((u, v)))] += 1
            directed[(u, v)] += 1
    boundary = sum(1 for count in edge_counts.values() if count == 1)
    nonmanifold = sum(1 for count in edge_counts.values() if count != 2)
    orientation_mismatched = sum(
        1
        for u, v in edge_counts
        if edge_counts[(u, v)] == 2
        and not (directed[(u, v)] == 1 and directed[(v, u)] == 1)
    )
    xs = [p[0] for p in mesh.vertices]
    ys = [p[1] for p in mesh.vertices]
    zs = [p[2] for p in mesh.vertices]
    return Validation(
        name=mesh.name,
        vertices=len(mesh.vertices),
        triangles=len(mesh.faces),
        boundary_edges=boundary,
        nonmanifold_edges=nonmanifold,
        orientation_mismatched_edges=orientation_mismatched,
        degenerate_triangles=degenerate,
        signed_volume_mm3=volume6 / 6.0,
        bbox_min_mm=(min(xs), min(ys), min(zs)),
        bbox_max_mm=(max(xs), max(ys), max(zs)),
    )


def point_triangle_distance(point: Vec3, a: Vec3, b: Vec3, c: Vec3) -> float:
    """Shortest point/triangle distance (Ericson region tests)."""
    ab, ac, ap = sub(b, a), sub(c, a), sub(point, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0.0 and d2 <= 0.0:
        return norm(ap)
    bp = sub(point, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0.0 and d4 <= d3:
        return norm(bp)
    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
        v = d1 / (d1 - d3)
        return norm(sub(point, (a[0] + v * ab[0], a[1] + v * ab[1], a[2] + v * ab[2])))
    cp = sub(point, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0.0 and d5 <= d6:
        return norm(cp)
    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
        w = d2 / (d2 - d6)
        return norm(sub(point, (a[0] + w * ac[0], a[1] + w * ac[1], a[2] + w * ac[2])))
    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        bc = sub(c, b)
        return norm(sub(point, (b[0] + w * bc[0], b[1] + w * bc[1], b[2] + w * bc[2])))
    denominator = 1.0 / (va + vb + vc)
    v, w = vb * denominator, vc * denominator
    closest = (
        a[0] + ab[0] * v + ac[0] * w,
        a[1] + ab[1] * v + ac[1] * w,
        a[2] + ab[2] * v + ac[2] * w,
    )
    return norm(sub(point, closest))


def shell_wall_check(mesh: Mesh, ring_count: int, facets: int = FACETS) -> dict[str, float]:
    """Sample both directions between corresponding outer/inner side surfaces."""
    ring_vertices = ring_count * facets
    side_triangles = (ring_count - 1) * facets * 2
    outer_vertices = mesh.vertices[:ring_vertices]
    inner_vertices = mesh.vertices[ring_vertices : 2 * ring_vertices]
    outer_faces = mesh.faces[:side_triangles]
    inner_faces = mesh.faces[side_triangles : 2 * side_triangles]
    outer_triangles = [tuple(mesh.vertices[i] for i in face) for face in outer_faces]
    inner_triangles = [tuple(mesh.vertices[i] for i in face) for face in inner_faces]
    inner_to_outer = min(
        point_triangle_distance(point, *triangle)
        for point in inner_vertices
        for triangle in outer_triangles
    )
    outer_to_inner = min(
        point_triangle_distance(point, *triangle)
        for point in outer_vertices
        for triangle in inner_triangles
    )
    return {
        "inner_vertex_to_outer_surface_min_mm": inner_to_outer,
        "outer_vertex_to_inner_surface_min_mm": outer_to_inner,
    }


def merged_mesh(name: str, meshes: Iterable[Mesh]) -> Mesh:
    result = Mesh(name)
    for mesh in meshes:
        offset = len(result.vertices)
        result.vertices.extend(mesh.vertices)
        result.faces.extend((a + offset, b + offset, c + offset) for a, b, c in mesh.faces)
    return result


def triangle_normal(mesh: Mesh, face: Tri) -> Vec3:
    a, b, c = (mesh.vertices[i] for i in face)
    n = cross(sub(b, a), sub(c, a))
    length = norm(n)
    if length == 0:
        return (0.0, 0.0, 0.0)
    return (n[0] / length, n[1] / length, n[2] / length)


def write_binary_stl(path: Path, name: str, meshes: Sequence[Mesh]) -> None:
    triangles = [(mesh, face) for mesh in meshes for face in mesh.faces]
    header = f"Low-poly orca {name}; units=mm".encode("ascii", "replace")[:80].ljust(80, b"\0")
    with path.open("wb") as f:
        f.write(header)
        f.write(struct.pack("<I", len(triangles)))
        for mesh, face in triangles:
            n = triangle_normal(mesh, face)
            values = [*n]
            for index in face:
                values.extend(mesh.vertices[index])
            f.write(struct.pack("<12fH", *values, 0))


def write_obj(path: Path, name: str, meshes: Sequence[Mesh]) -> None:
    lines = [f"# {name} low-poly orca; units: mm"]
    offset = 1
    for mesh in meshes:
        lines.append(f"o {mesh.name}")
        lines.extend(f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in mesh.vertices)
        lines.extend(f"f {a + offset} {b + offset} {c + offset}" for a, b, c in mesh.faces)
        offset += len(mesh.vertices)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class StepWriter:
    """Small AP203 FACETED_BREP writer with optional product hierarchy."""

    def __init__(self) -> None:
        self.entities: list[str] = []

    def add(self, expression: str) -> int:
        self.entities.append(expression)
        return len(self.entities)

    def ref(self, entity: int) -> str:
        return f"#{entity}"

    def point(self, p: Vec3) -> int:
        return self.add(f"CARTESIAN_POINT('',({fmt(p[0])},{fmt(p[1])},{fmt(p[2])}))")

    def faceted_brep(self, mesh: Mesh) -> int:
        points = [self.point(p) for p in mesh.vertices]
        faces = []
        for a, b, c in mesh.faces:
            pa, pb, pc = mesh.vertices[a], mesh.vertices[b], mesh.vertices[c]
            normal = cross(sub(pb, pa), sub(pc, pa))
            normal_length = norm(normal)
            edge = sub(pb, pa)
            edge_length = norm(edge)
            if normal_length < 1e-12 or edge_length < 1e-12:
                raise ValueError(f"{mesh.name}: cannot create a plane for a degenerate triangle")
            normal_direction = self.add(
                "DIRECTION('',("
                f"{fmt(normal[0] / normal_length)},"
                f"{fmt(normal[1] / normal_length)},"
                f"{fmt(normal[2] / normal_length)}))"
            )
            reference_direction = self.add(
                "DIRECTION('',("
                f"{fmt(edge[0] / edge_length)},"
                f"{fmt(edge[1] / edge_length)},"
                f"{fmt(edge[2] / edge_length)}))"
            )
            placement = self.add(
                f"AXIS2_PLACEMENT_3D('',{self.ref(points[a])},"
                f"{self.ref(normal_direction)},{self.ref(reference_direction)})"
            )
            plane = self.add(f"PLANE('',{self.ref(placement)})")
            loop = self.add(
                f"POLY_LOOP('',({self.ref(points[a])},{self.ref(points[b])},{self.ref(points[c])}))"
            )
            bound = self.add(f"FACE_OUTER_BOUND('',{self.ref(loop)},.T.)")
            faces.append(
                self.add(
                    f"FACE_SURFACE('',({self.ref(bound)}),{self.ref(plane)},.T.)"
                )
            )
        shell = self.add(f"CLOSED_SHELL('{step_escape(mesh.name)}',({','.join(self.ref(f) for f in faces)}))")
        return self.add(f"FACETED_BREP('{step_escape(mesh.name)}',{self.ref(shell)})")

    def units_context(self) -> int:
        length = self.add("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))")
        angle = self.add("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
        solid_angle = self.add("(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())")
        uncertainty = self.add(
            f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.001),{self.ref(length)},'distance_accuracy_value','model accuracy')"
        )
        return self.add(
            "(GEOMETRIC_REPRESENTATION_CONTEXT(3)"
            f"GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT(({self.ref(uncertainty)}))"
            f"GLOBAL_UNIT_ASSIGNED_CONTEXT(({self.ref(length)},{self.ref(angle)},{self.ref(solid_angle)}))"
            "REPRESENTATION_CONTEXT('orca','3D'))"
        )

    def axis(self, name: str = "") -> int:
        origin = self.point((0.0, 0.0, 0.0))
        zdir = self.add("DIRECTION('',(0.,0.,1.))")
        xdir = self.add("DIRECTION('',(1.,0.,0.))")
        return self.add(
            f"AXIS2_PLACEMENT_3D('{step_escape(name)}',{self.ref(origin)},{self.ref(zdir)},{self.ref(xdir)})"
        )

    def product(self, app_context: int, name: str, category: str = "detail") -> tuple[int, int]:
        product_context = self.add(f"MECHANICAL_CONTEXT('',{self.ref(app_context)},'mechanical')")
        product = self.add(
            f"PRODUCT('{step_escape(name)}','{step_escape(name)}','',({self.ref(product_context)}))"
        )
        self.add(
            f"PRODUCT_RELATED_PRODUCT_CATEGORY('{step_escape(category)}','',({self.ref(product)}))"
        )
        formation = self.add(
            f"PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('','',{self.ref(product)},.NOT_KNOWN.)"
        )
        definition_context = self.add(
            f"DESIGN_CONTEXT('part definition',{self.ref(app_context)},'design')"
        )
        definition = self.add(
            f"PRODUCT_DEFINITION('design','',{self.ref(formation)},{self.ref(definition_context)})"
        )
        shape = self.add(f"PRODUCT_DEFINITION_SHAPE('','',{self.ref(definition)})")
        return definition, shape


def fmt(value: float) -> str:
    if abs(value) < 5e-10:
        value = 0.0
    text = f"{value:.9f}".rstrip("0").rstrip(".")
    return text + "." if "." not in text else text


def step_escape(value: str) -> str:
    return value.replace("'", "''")


def step_header(filename: str) -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return "\n".join(
        [
            "ISO-10303-21;",
            "HEADER;",
            "FILE_DESCRIPTION(('Three-piece low-poly orca; dimensions in millimetres'),'2;1');",
            f"FILE_NAME('{step_escape(filename)}','{stamp}',('OpenAI'),('OpenAI'),'Python FACETED_BREP generator','','');",
            "FILE_SCHEMA(('CONFIG_CONTROL_DESIGN'));",
            "ENDSEC;",
            "DATA;",
        ]
    )


def emit_step(path: Path, entities: Sequence[str]) -> None:
    body = "\n".join(f"#{i}={entity};" for i, entity in enumerate(entities, start=1))
    path.write_text(step_header(path.name) + "\n" + body + "\nENDSEC;\nEND-ISO-10303-21;\n", encoding="ascii")


def write_part_step(path: Path, part: Part) -> None:
    sw = StepWriter()
    app = sw.add("APPLICATION_CONTEXT('configuration controlled 3d designs of mechanical parts and assemblies')")
    sw.add(f"APPLICATION_PROTOCOL_DEFINITION('international standard','config_control_design',1994,{sw.ref(app)})")
    _definition, product_shape = sw.product(app, part.name)
    context = sw.units_context()
    breps = [sw.faceted_brep(mesh) for mesh in part.solids]
    representation = sw.add(
        f"FACETED_BREP_SHAPE_REPRESENTATION('{step_escape(part.name)}',({','.join(sw.ref(b) for b in breps)}),{sw.ref(context)})"
    )
    sw.add(f"SHAPE_DEFINITION_REPRESENTATION({sw.ref(product_shape)},{sw.ref(representation)})")
    emit_step(path, sw.entities)


def write_assembly_step(path: Path, parts: Sequence[Part]) -> None:
    """Write an AP203 assembly with identity transforms and named child parts."""
    sw = StepWriter()
    app = sw.add("APPLICATION_CONTEXT('configuration controlled 3d designs of mechanical parts and assemblies')")
    sw.add(f"APPLICATION_PROTOCOL_DEFINITION('international standard','config_control_design',1994,{sw.ref(app)})")
    assembly_definition, assembly_shape = sw.product(app, "Whale_Assembly", "assembly")
    context = sw.units_context()
    assembly_axis = sw.axis("Assembly_Origin")
    assembly_rep = sw.add(
        f"SHAPE_REPRESENTATION('Whale_Assembly',({sw.ref(assembly_axis)}),{sw.ref(context)})"
    )
    sw.add(f"SHAPE_DEFINITION_REPRESENTATION({sw.ref(assembly_shape)},{sw.ref(assembly_rep)})")

    for occurrence_index, part in enumerate(parts, start=1):
        part_definition, part_shape = sw.product(app, part.name)
        part_axis = sw.axis(f"{part.name}_Origin")
        breps = [sw.faceted_brep(mesh) for mesh in part.solids]
        part_rep = sw.add(
            f"FACETED_BREP_SHAPE_REPRESENTATION('{step_escape(part.name)}',"
            f"({sw.ref(part_axis)},{','.join(sw.ref(b) for b in breps)}),{sw.ref(context)})"
        )
        sw.add(f"SHAPE_DEFINITION_REPRESENTATION({sw.ref(part_shape)},{sw.ref(part_rep)})")
        usage = sw.add(
            f"NEXT_ASSEMBLY_USAGE_OCCURRENCE('{occurrence_index}','{step_escape(part.name)}','',"
            f"{sw.ref(assembly_definition)},{sw.ref(part_definition)},$)"
        )
        usage_shape = sw.add(
            f"PRODUCT_DEFINITION_SHAPE('Placement','Placement of {step_escape(part.name)}',{sw.ref(usage)})"
        )
        transform = sw.add(
            f"ITEM_DEFINED_TRANSFORMATION('{step_escape(part.name)} identity','',"
            f"{sw.ref(assembly_axis)},{sw.ref(part_axis)})"
        )
        relation = sw.add(
            "(REPRESENTATION_RELATIONSHIP('','',"
            f"{sw.ref(assembly_rep)},{sw.ref(part_rep)})"
            f"REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION({sw.ref(transform)})"
            "SHAPE_REPRESENTATION_RELATIONSHIP())"
        )
        sw.add(f"CONTEXT_DEPENDENT_SHAPE_REPRESENTATION({sw.ref(relation)},{sw.ref(usage_shape)})")
    emit_step(path, sw.entities)


def write_preview(path: Path, parts: Sequence[Part]) -> bool:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    except Exception:
        return False

    fig = plt.figure(figsize=(16, 10), dpi=160, facecolor="#eef1f3")
    base_colours = {"Head": "#151a20", "Body": "#222b33", "Tail": "#303b45"}

    def mesh_colour(part_name: str, mesh_name: str) -> str:
        if "Hatch" in mesh_name:
            return "#596671"
        if "Seam" in mesh_name:
            return "#596671"
        if "Panel" in mesh_name:
            return "#46515a"
        if "Louver" in mesh_name or "Strake" in mesh_name:
            return "#6d7d88"
        if "Nozzle" in mesh_name:
            return "#101317"
        if "Dorsal" in mesh_name or "Pectoral" in mesh_name:
            return "#353f48"
        return base_colours[part_name]

    def scene(position: int, title: str, elev: float, azim: float) -> None:
        ax = fig.add_subplot(2, 2, position, projection="3d")
        for part in parts:
            for mesh in part.solids:
                polys = [[mesh.vertices[i] for i in face] for face in mesh.faces]
                highlighted = any(
                    token in mesh.name
                    for token in ("Hatch", "Panel", "Louver", "Strake", "Nozzle", "Seam")
                )
                collection = Poly3DCollection(
                    polys,
                    facecolor=mesh_colour(part.name, mesh.name),
                    edgecolor="#aebbc4" if highlighted else "#7f909d",
                    linewidth=0.34 if highlighted else 0.16,
                    alpha=1.0,
                )
                ax.add_collection3d(collection)
        for seam_profile in (
            (120.0, 89.0, 77.0, 13.0, 2.4),
            (300.0, 60.0, 55.5, 8.5, 2.2),
        ):
            seam = [
                superellipse_point(
                    *seam_profile,
                    2.0 * math.pi * index / 72,
                )
                for index in range(73)
            ]
            ax.plot(
                [point[0] for point in seam],
                [point[1] for point in seam],
                [point[2] for point in seam],
                color="#d3a15f",
                linewidth=1.0,
            )
        ax.set_xlim(0, 420)
        ax.set_ylim(-135, 135)
        ax.set_zlim(-72, 135)
        ax.set_box_aspect((420, 270, 207))
        ax.view_init(elev=elev, azim=azim)
        ax.set_title(title, fontsize=11, pad=2)
        ax.set_axis_off()

    scene(1, "Perspective", 18, -58)
    scene(2, "Side profile", 0, -90)
    scene(3, "Top profile", 90, -90)
    scene(4, "Stern / twin nozzle cues", 0, 0)
    fig.suptitle(
        "420 mm compact three-piece low-poly biomimetic underwater thruster — V2",
        fontsize=16,
        y=0.97,
    )
    fig.text(0.5, 0.02, "X = 0 mm blunt nose  →  X = 420 mm stern", ha="center", fontsize=10)
    fig.tight_layout(rect=(0.01, 0.04, 0.99, 0.94))
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return True


def verify_step_references(path: Path) -> tuple[int, int, list[int]]:
    import re

    text = path.read_text(encoding="ascii")
    definitions = {int(x) for x in re.findall(r"^#(\d+)=", text, flags=re.MULTILINE)}
    right_hand_sides = re.sub(r"^#\d+=", "", text, flags=re.MULTILINE)
    references = {int(x) for x in re.findall(r"#(\d+)", right_hand_sides)}
    missing = sorted(references - definitions)
    return len(definitions), len(references), missing


def build(output_dir: Path) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    step_dir = output_dir / "STEP"
    stl_dir = output_dir / "STL"
    obj_dir = output_dir / "OBJ"
    for directory in (step_dir, stl_dir, obj_dir):
        directory.mkdir(exist_ok=True)

    parts_by_name = make_parts()
    parts = list(parts_by_name.values())
    validations = [validate_mesh(mesh) for part in parts for mesh in part.solids]
    failures = [v for v in validations if not v.watertight or v.signed_volume_mm3 <= 0]
    if failures:
        details = ", ".join(f"{v.name}: watertight={v.watertight}, volume={v.signed_volume_mm3}" for v in failures)
        raise RuntimeError(f"Geometry validation failed: {details}")

    wall_checks = {
        part.name: shell_wall_check(part.solids[0], ring_count=6)
        for part in parts
    }
    for part_name, check in wall_checks.items():
        if min(check.values()) < WALL_MM - 1e-6:
            raise RuntimeError(f"{part_name}: sampled shell wall is below {WALL_MM} mm: {check}")

    def cap_axial_gap(mesh: Mesh, ring_count: int, ring_index: int) -> dict[str, float]:
        outer_base = ring_index * FACETS
        inner_base = ring_count * FACETS + ring_index * FACETS
        distances = [
            abs(mesh.vertices[outer_base + index][0] - mesh.vertices[inner_base + index][0])
            for index in range(FACETS)
        ]
        return {"min_mm": min(distances), "max_mm": max(distances)}

    cap_wall_checks = {
        "Head_nose_cap": cap_axial_gap(parts_by_name["Head"].solids[0], 6, 0),
        "Tail_stern_cap": cap_axial_gap(parts_by_name["Tail"].solids[0], 6, 5),
    }
    for cap_name, check in cap_wall_checks.items():
        if check["min_mm"] < WALL_MM - 1e-6:
            raise RuntimeError(f"{cap_name}: axial cap wall is below {WALL_MM} mm: {check}")

    solids_by_name = {
        mesh.name: mesh
        for part in parts
        for mesh in part.solids
    }
    joint_wall_ring_counts = {
        "Joint120_Male_Collar": 3,
        "Joint120_Female_Socket": 2,
        "Joint300_Male_Collar": 3,
        "Joint300_Female_Socket": 2,
    }
    joint_wall_checks = {
        name: shell_wall_check(solids_by_name[name], ring_count=ring_count)
        for name, ring_count in joint_wall_ring_counts.items()
    }
    for joint_name, check in joint_wall_checks.items():
        if min(check.values()) < WALL_MM - 1e-6:
            raise RuntimeError(
                f"{joint_name}: sampled locating-joint wall is below {WALL_MM} mm: {check}"
            )

    joint_axis_checks = {
        "joint120": {
            "upstream_pin_center_y_mm": 65.0,
            "downstream_pin_center_y_mm": 65.0,
            "upstream_pin_center_z_mm": 13.0,
            "downstream_pin_center_z_mm": 13.0,
        },
        "joint300": {
            "upstream_pin_center_y_mm": 42.0,
            "downstream_pin_center_y_mm": 42.0,
            "upstream_pin_center_z_mm": 8.5,
            "downstream_pin_center_z_mm": 8.5,
        },
    }
    for joint_name, check in joint_axis_checks.items():
        delta_y = abs(check["upstream_pin_center_y_mm"] - check["downstream_pin_center_y_mm"])
        delta_z = abs(check["upstream_pin_center_z_mm"] - check["downstream_pin_center_z_mm"])
        check["axis_delta_y_mm"] = delta_y
        check["axis_delta_z_mm"] = delta_z
        if max(delta_y, delta_z) > 0.02:
            raise RuntimeError(f"{joint_name}: locating-pin axes are misaligned: {check}")

    fit_gap_checks = {
        "joint120_interface_x120": mesh_joint_clearance_stats(
            solids_by_name["Joint120_Female_Socket"],
            2,
            solids_by_name["Joint120_Male_Collar"],
            3,
            120.0,
        ),
        "joint120_lip_tip_x128": mesh_joint_clearance_stats(
            solids_by_name["Joint120_Female_Socket"],
            2,
            solids_by_name["Joint120_Male_Collar"],
            3,
            128.0,
        ),
        "joint300_interface_x300": mesh_joint_clearance_stats(
            solids_by_name["Joint300_Female_Socket"],
            2,
            solids_by_name["Joint300_Male_Collar"],
            3,
            300.0,
        ),
        "joint300_lip_tip_x308": mesh_joint_clearance_stats(
            solids_by_name["Joint300_Female_Socket"],
            2,
            solids_by_name["Joint300_Male_Collar"],
            3,
            308.0,
        ),
    }
    fit_low = FIT_RADIAL_CLEARANCE_MM - FIT_TOLERANCE_MM
    fit_high = FIT_RADIAL_CLEARANCE_MM + FIT_TOLERANCE_MM
    for location, check in fit_gap_checks.items():
        if not check["male_inside_female"] or check["boundary_intersection"]:
            raise RuntimeError(f"{location}: locating-lip section interferes: {check}")
        if check["min_mm"] < fit_low - 1e-6 or check["max_mm"] > fit_high + 1e-6:
            raise RuntimeError(
                f"{location}: actual mesh locating-lip gap {check} is outside "
                f"{fit_low:.3f}-{fit_high:.3f} mm"
            )

    for part in parts:
        write_part_step(step_dir / f"{part.name}.step", part)
        write_binary_stl(stl_dir / f"{part.name}.stl", part.name, part.solids)
        write_obj(obj_dir / f"{part.name}.obj", part.name, part.solids)

    all_solids = [mesh for part in parts for mesh in part.solids]
    whale = Part("Whale", all_solids)
    write_part_step(step_dir / "Whale.step", whale)
    write_assembly_step(step_dir / "Whale_Assembly.step", parts)
    write_binary_stl(stl_dir / "Whale.stl", "Whale", all_solids)
    write_obj(obj_dir / "Whale.obj", "Whale", all_solids)
    preview_written = write_preview(output_dir / "Whale_preview.png", parts)

    step_checks = {}
    for step_path in sorted(step_dir.glob("*.step")):
        definitions, references, missing = verify_step_references(step_path)
        step_checks[step_path.name] = {
            "entity_definitions": definitions,
            "unique_referenced_entities": references,
            "missing_entity_ids": missing,
        }
        if missing:
            raise RuntimeError(f"{step_path.name}: dangling STEP references {missing}")

    aggregate = merged_mesh("Whale", all_solids)
    xs = [v[0] for v in aggregate.vertices]
    ys = [v[1] for v in aggregate.vertices]
    zs = [v[2] for v in aggregate.vertices]
    manifest = {
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "design": {
            "revision": "V2_reference_driven_compact_underwater_thruster",
            "overall_x_mm": [min(xs), max(xs)],
            "nominal_length_mm": 420.0,
            "body_max_diameter_mm": 180.0,
            "segments_mm": {"Head": [0, 120], "Body": [120, 300], "Tail": [300, 420]},
            "wall_mm": WALL_MM,
            "cross_section_facets": FACETS,
            "shell_cross_section": "vertically shifted low-poly superellipse",
            "fit_radial_clearance_mm": FIT_RADIAL_CLEARANCE_MM,
            "fit_dimension_tolerance_mm": FIT_TOLERANCE_MM,
            "assembly_hole_diameter_mm": ASSEMBLY_HOLE_DIAMETER_MM,
            "reference_features": [
                "blunt nose",
                "high crown and flatter belly",
                "integrated tapered multi-panel top hatch cue",
                "low swept dorsal and pectoral control surfaces",
                "raised non-penetrating intake and vent strakes",
                "truncated stern with twin decorative nozzle shrouds",
            ],
            "functional_openings": False,
            "overall_bbox_with_fins_mm": {
                "min": [min(xs), min(ys), min(zs)],
                "max": [max(xs), max(ys), max(zs)],
            },
        },
        "parts": {
            part.name: {
                "solid_count": len(part.solids),
                "solids": [mesh.name for mesh in part.solids],
                "triangle_count": sum(len(mesh.faces) for mesh in part.solids),
            }
            for part in parts
        },
        "validation": [asdict(v) | {"watertight": v.watertight} for v in validations],
        "wall_distance_checks_mm": wall_checks,
        "cap_wall_checks_mm": cap_wall_checks,
        "joint_wall_checks_mm": joint_wall_checks,
        "joint_axis_checks_mm": joint_axis_checks,
        "fit_gap_checks_mm": fit_gap_checks,
        "step_reference_checks": step_checks,
        "preview_written": preview_written,
        "native_solidworks_files": "Created only by solidworks_2025_build.py on Windows with SOLIDWORKS 2025.",
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    report_lines = [
        "ORCA V2 GEOMETRY VALIDATION",
        "===========================",
        "Revision: reference-driven compact underwater thruster",
        f"Nominal X length: {max(xs) - min(xs):.3f} mm",
        "Body maximum diameter: 180.000 mm",
        f"Wall: {WALL_MM:.3f} mm",
        f"Fit radial clearance: {FIT_RADIAL_CLEARANCE_MM:.3f} mm (dimension tolerance +/-{FIT_TOLERANCE_MM:.3f} mm)",
        f"Assembly holes: {ASSEMBLY_HOLE_DIAMETER_MM:.3f} mm",
        "",
    ]
    report_lines.extend(
        f"{name}: actual mesh locating-lip gap "
        f"{check['min_mm']:.3f}-{check['max_mm']:.3f} mm "
        f"(female/male section vertices {check['female_section_vertices']}/"
        f"{check['male_section_vertices']})"
        for name, check in fit_gap_checks.items()
    )
    report_lines.append("")
    report_lines.extend(
        f"{name}: axial cap wall {check['min_mm']:.3f}-{check['max_mm']:.3f} mm"
        for name, check in cap_wall_checks.items()
    )
    report_lines.append("")
    report_lines.extend(
        f"{name}: sampled joint wall "
        f"{check['inner_vertex_to_outer_surface_min_mm']:.3f}-"
        f"{check['outer_vertex_to_inner_surface_min_mm']:.3f} mm"
        for name, check in joint_wall_checks.items()
    )
    report_lines.append("")
    report_lines.extend(
        f"{name}: locating-pin axis delta Y={check['axis_delta_y_mm']:.3f} mm, "
        f"Z={check['axis_delta_z_mm']:.3f} mm"
        for name, check in joint_axis_checks.items()
    )
    report_lines.append("")
    report_lines.extend(
        f"{v.name}: V={v.vertices}, T={v.triangles}, watertight={v.watertight}, "
        f"boundary={v.boundary_edges}, nonmanifold={v.nonmanifold_edges}, "
        f"orientation_mismatch={v.orientation_mismatched_edges}, "
        f"volume={v.signed_volume_mm3:.3f} mm^3"
        for v in validations
    )
    report_lines.append("")
    report_lines.append("WALL DISTANCE SAMPLES")
    report_lines.extend(
        f"{part_name}: inner->outer={check['inner_vertex_to_outer_surface_min_mm']:.6f} mm, "
        f"outer->inner={check['outer_vertex_to_inner_surface_min_mm']:.6f} mm"
        for part_name, check in wall_checks.items()
    )
    (output_dir / "VALIDATION.txt").write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "output",
        help="Output directory",
    )
    args = parser.parse_args()
    manifest = build(args.output.resolve())
    print(json.dumps({"output": str(args.output.resolve()), "design": manifest["design"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
