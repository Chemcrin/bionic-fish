from __future__ import annotations

import math
from pathlib import Path

import cadquery as cq


# Units: millimeters.
P = {
    "fixed_axis_distance": 44.0,   # O2 tail axis to O1 motor axis
    "crank_radius": 10.0,          # O1 to crank pin
    "connecting_rod_length": 35.0, # crank pin to rocker pin
    "rocker_length": 26.0,         # O2 tail axis to rocker pin
    "crank_angle_deg": 135.0,      # assembly pose for visualization
    "pin_hole_d": 3.2,             # clearance for M3 pin/screw
    "tail_shaft_d": 5.0,
    "tail_shaft_clearance_d": 5.3,
    "arm_plane_z": 28.0,
    "link_plane_z": 34.0,
}


OUT_DIR = Path(__file__).resolve().parent / "crank_rocker_step"


def capsule(length: float, width: float, thickness: float) -> cq.Workplane:
    """Rounded flat bar along local X, centered at origin."""
    r = width / 2.0
    body = cq.Workplane("XY").box(length, width, thickness)
    end_a = (
        cq.Workplane("XY")
        .center(-length / 2.0, 0)
        .circle(r)
        .extrude(thickness)
        .translate((0, 0, -thickness / 2.0))
    )
    end_b = (
        cq.Workplane("XY")
        .center(length / 2.0, 0)
        .circle(r)
        .extrude(thickness)
        .translate((0, 0, -thickness / 2.0))
    )
    return body.union(end_a).union(end_b)


def flat_link(
    center_distance: float,
    width: float,
    thickness: float,
    hole_a_d: float,
    hole_b_d: float,
    boss_a_d: float | None = None,
    boss_b_d: float | None = None,
    boss_h: float = 1.5,
) -> cq.Workplane:
    """Two-hole rounded link; local hole centers are at +/- center_distance / 2."""
    obj = capsule(center_distance, width, thickness)

    if boss_a_d:
        obj = obj.union(
            cq.Workplane("XY")
            .center(-center_distance / 2.0, 0)
            .circle(boss_a_d / 2.0)
            .extrude(thickness + boss_h)
            .translate((0, 0, -(thickness + boss_h) / 2.0))
        )
    if boss_b_d:
        obj = obj.union(
            cq.Workplane("XY")
            .center(center_distance / 2.0, 0)
            .circle(boss_b_d / 2.0)
            .extrude(thickness + boss_h)
            .translate((0, 0, -(thickness + boss_h) / 2.0))
        )

    return (
        obj.faces(">Z")
        .workplane()
        .pushPoints([(-center_distance / 2.0, 0), (center_distance / 2.0, 0)])
        .hole(hole_a_d)
        if abs(hole_a_d - hole_b_d) < 1e-6
        else obj.faces(">Z")
        .workplane()
        .pushPoints([(-center_distance / 2.0, 0)])
        .hole(hole_a_d)
        .faces(">Z")
        .workplane()
        .pushPoints([(center_distance / 2.0, 0)])
        .hole(hole_b_d)
    )


def make_crank() -> cq.Workplane:
    crank = flat_link(
        P["crank_radius"],
        width=12.0,
        thickness=5.0,
        hole_a_d=P["tail_shaft_clearance_d"],
        hole_b_d=P["pin_hole_d"],
        boss_a_d=16.0,
        boss_b_d=8.0,
        boss_h=2.0,
    )
    # Move local motor axis from -L/2 to origin; pin then lies at +crank_radius.
    return crank.translate((P["crank_radius"] / 2.0, 0, 0))


def make_connecting_rod() -> cq.Workplane:
    rod = flat_link(
        P["connecting_rod_length"],
        width=7.0,
        thickness=3.0,
        hole_a_d=P["pin_hole_d"],
        hole_b_d=P["pin_hole_d"],
        boss_a_d=9.0,
        boss_b_d=9.0,
        boss_h=1.0,
    )
    return rod


def make_rocker() -> cq.Workplane:
    rocker = flat_link(
        P["rocker_length"],
        width=13.0,
        thickness=6.0,
        hole_a_d=P["tail_shaft_clearance_d"],
        hole_b_d=P["pin_hole_d"],
        boss_a_d=18.0,
        boss_b_d=9.0,
        boss_h=2.0,
    )
    # Move local tail axis from -L/2 to origin; rod pin then lies at +rocker_length.
    rocker = rocker.translate((P["rocker_length"] / 2.0, 0, 0))
    clamp_slot = cq.Workplane("XY").box(13.0, 1.2, 9.0).translate((0, -6.4, 0))
    return rocker.cut(clamp_slot)


def make_tail_shaft() -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .circle(P["tail_shaft_d"] / 2.0)
        .extrude(42.0)
        .translate((0, 0, 4.0))
    )


def make_tail_output_horn() -> cq.Workplane:
    collar = (
        cq.Workplane("XY")
        .circle(8.5)
        .extrude(9.0)
        .translate((0, 0, -4.5))
        .faces(">Z")
        .workplane()
        .hole(P["tail_shaft_clearance_d"])
    )
    beam = (
        capsule(40.0, 10.0, 6.0)
        .translate((-20.0, 0, 0))
        .faces(">Z")
        .workplane()
        .pushPoints([(-34.0, 0), (-22.0, 0)])
        .hole(2.6)
    )
    return collar.union(beam)


def make_frame() -> cq.Workplane:
    d = P["fixed_axis_distance"]
    base = cq.Workplane("XY").box(d + 36.0, 36.0, 4.0).translate((d / 2.0, 0, 2.0))

    tail_bearing = (
        cq.Workplane("XY")
        .circle(7.5)
        .extrude(30.0)
        .translate((0, 0, 4.0))
        .faces(">Z")
        .workplane()
        .hole(P["tail_shaft_clearance_d"])
    )
    tail_foot = cq.Workplane("XY").box(22.0, 22.0, 6.0).translate((0, 0, 7.0))

    motor_block = cq.Workplane("XY").box(24.0, 24.0, 18.0).translate((d, 0, 13.0))
    motor_top = cq.Workplane("XY").box(34.0, 30.0, 4.0).translate((d, 0, 24.0))
    motor_bore = cq.Workplane("XY").center(d, 0).circle(6.0).extrude(30.0)
    motor_screw_holes = (
        cq.Workplane("XY")
        .pushPoints([(d - 8.0, -8.0), (d + 8.0, -8.0), (d - 8.0, 8.0), (d + 8.0, 8.0)])
        .circle(1.5)
        .extrude(30.0)
    )

    bridge = cq.Workplane("XY").box(d + 10.0, 7.0, 5.0).translate((d / 2.0, -14.5, 24.5))

    frame = base.union(tail_foot).union(tail_bearing).union(motor_block).union(motor_top).union(bridge)
    return frame.cut(motor_bore).cut(motor_screw_holes)


def make_pin(height: float) -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .circle(1.5)
        .extrude(height)
        .union(cq.Workplane("XY").circle(2.8).extrude(1.0).translate((0, 0, height)))
    )


def circle_intersections(
    c0: tuple[float, float],
    r0: float,
    c1: tuple[float, float],
    r1: float,
) -> tuple[tuple[float, float], tuple[float, float]]:
    x0, y0 = c0
    x1, y1 = c1
    dx, dy = x1 - x0, y1 - y0
    dist = math.hypot(dx, dy)
    a = (r0 * r0 - r1 * r1 + dist * dist) / (2.0 * dist)
    h = math.sqrt(max(r0 * r0 - a * a, 0.0))
    xm = x0 + a * dx / dist
    ym = y0 + a * dy / dist
    rx = -dy * h / dist
    ry = dx * h / dist
    return (xm + rx, ym + ry), (xm - rx, ym - ry)


def place_xy(obj: cq.Workplane, x: float, y: float, z: float, angle_deg: float) -> cq.Workplane:
    return obj.rotate((0, 0, 0), (0, 0, 1), angle_deg).translate((x, y, z))


def assembly_geometry() -> dict[str, object]:
    d = P["fixed_axis_distance"]
    a = P["crank_radius"]
    b = P["connecting_rod_length"]
    c = P["rocker_length"]
    theta = math.radians(P["crank_angle_deg"])

    o2 = (0.0, 0.0)
    o1 = (d, 0.0)
    crank_pin = (o1[0] + a * math.cos(theta), o1[1] + a * math.sin(theta))
    candidates = circle_intersections(o2, c, crank_pin, b)
    rocker_pin = max(candidates, key=lambda p: p[1])
    rocker_angle = math.degrees(math.atan2(rocker_pin[1] - o2[1], rocker_pin[0] - o2[0]))
    rod_angle = math.degrees(math.atan2(rocker_pin[1] - crank_pin[1], rocker_pin[0] - crank_pin[0]))
    rod_mid = ((crank_pin[0] + rocker_pin[0]) / 2.0, (crank_pin[1] + rocker_pin[1]) / 2.0)

    return {
        "o1": o1,
        "o2": o2,
        "crank_pin": crank_pin,
        "rocker_pin": rocker_pin,
        "rocker_angle_deg": rocker_angle,
        "rod_angle_deg": rod_angle,
        "rod_mid": rod_mid,
    }


def export_step(obj: cq.Workplane | cq.Assembly, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(obj, cq.Assembly):
        obj.save(str(path))
    else:
        cq.exporters.export(obj, str(path))


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    frame = make_frame()
    crank = make_crank()
    rod = make_connecting_rod()
    rocker = make_rocker()
    tail_shaft = make_tail_shaft()
    tail_horn = make_tail_output_horn()

    parts = {
        "01_frame_tail_motor_support.step": frame,
        "02_motor_crank.step": crank,
        "03_connecting_rod.step": rod,
        "04_tail_rocker.step": rocker,
        "05_tail_shaft.step": tail_shaft,
        "06_tail_output_horn.step": tail_horn,
        "07_m3_joint_pin.step": make_pin(10.0),
    }
    for filename, part in parts.items():
        export_step(part, OUT_DIR / filename)

    g = assembly_geometry()
    crank_placed = place_xy(crank, *g["o1"], P["arm_plane_z"], P["crank_angle_deg"])
    rocker_placed = place_xy(rocker, *g["o2"], P["arm_plane_z"], g["rocker_angle_deg"])
    rod_placed = place_xy(rod, *g["rod_mid"], P["link_plane_z"], g["rod_angle_deg"])
    shaft_placed = tail_shaft
    horn_placed = place_xy(tail_horn, *g["o2"], 43.0, g["rocker_angle_deg"])
    pin_a = make_pin(10.0).translate((g["crank_pin"][0], g["crank_pin"][1], P["arm_plane_z"] + 2.0))
    pin_b = make_pin(10.0).translate((g["rocker_pin"][0], g["rocker_pin"][1], P["arm_plane_z"] + 2.0))

    assy = cq.Assembly(name="bionic_fish_crank_rocker")
    assy.add(frame, name="frame_tail_motor_support", color=cq.Color(0.35, 0.37, 0.38))
    assy.add(shaft_placed, name="tail_shaft", color=cq.Color(0.70, 0.70, 0.68))
    assy.add(crank_placed, name="motor_crank", color=cq.Color(1.00, 0.38, 0.05))
    assy.add(rocker_placed, name="tail_rocker", color=cq.Color(0.22, 0.25, 0.27))
    assy.add(rod_placed, name="connecting_rod", color=cq.Color(0.55, 0.58, 0.58))
    assy.add(horn_placed, name="tail_output_horn", color=cq.Color(0.18, 0.20, 0.22))
    assy.add(pin_a, name="crank_joint_pin", color=cq.Color(0.85, 0.85, 0.82))
    assy.add(pin_b, name="rocker_joint_pin", color=cq.Color(0.85, 0.85, 0.82))
    export_step(assy, OUT_DIR / "00_crank_rocker_assembly.step")

    readme = OUT_DIR / "parameters.txt"
    readme.write_text(
        "\n".join(
            [
                "Bionic fish crank-rocker mechanism",
                "Units: mm",
                f"Fixed axis distance O1-O2: {P['fixed_axis_distance']}",
                f"Crank radius O1-A: {P['crank_radius']}",
                f"Connecting rod A-B: {P['connecting_rod_length']}",
                f"Rocker length O2-B: {P['rocker_length']}",
                f"Assembly crank angle: {P['crank_angle_deg']} deg",
                f"Computed rocker angle in exported assembly: {g['rocker_angle_deg']:.2f} deg",
                f"Pin holes: {P['pin_hole_d']} dia, tail shaft clearance: {P['tail_shaft_clearance_d']} dia",
            ]
        ),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
