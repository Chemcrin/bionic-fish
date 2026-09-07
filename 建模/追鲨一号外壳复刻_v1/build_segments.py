"""Build and split the Zhuisha No.1 exterior mesh for SolidWorks import.

The source model is intentionally kept as the geometric authority.  This
script exports the complete mesh and three visual/printing sections.  Section
cuts are made at X = 180 and 520 mm (the 700 mm chase-vehicle model), not at
the 420 mm dimensions from the older whale prompt.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "source"
OUT = HERE / "out"
sys.path.insert(0, str(SOURCE))

import geom as G  # noqa: E402
import model as M  # noqa: E402


SEGMENTS = (
    ("head", 0.0, 180.0),
    ("body", 180.0, 520.0),
    ("tail", 520.0, 710.0),
)


def write_binary_stl(path: Path, vertices: np.ndarray, faces: np.ndarray) -> None:
    """Write a binary STL in millimetres, preserving the source winding."""
    a = vertices[faces[:, 0]]
    b = vertices[faces[:, 1]]
    c = vertices[faces[:, 2]]
    n = np.cross(b - a, c - a)
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    n = n / np.where(ln < 1.0e-12, 1.0, ln)
    rec = np.zeros(
        len(faces),
        dtype=np.dtype(
            [
                ("n", "<f4", 3),
                ("a", "<f4", 3),
                ("b", "<f4", 3),
                ("c", "<f4", 3),
                ("att", "<u2"),
            ]
        ),
    )
    rec["n"], rec["a"], rec["b"], rec["c"] = n, a, b, c
    with path.open("wb") as fh:
        fh.write(b"ZHUISHA-No1 segmented exterior shell".ljust(80, b" "))
        fh.write(np.uint32(len(faces)).tobytes())
        fh.write(rec.tobytes())


def compact_mesh(vertices: np.ndarray, faces: np.ndarray):
    """Drop unused vertices after selecting triangles."""
    used = np.unique(faces.reshape(-1))
    remap = np.full(len(vertices), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return vertices[used], remap[faces]


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    scene, scale = M.build()
    vertices, faces, _, _ = scene.merged()
    lo, hi = scene.bbox()
    dims = hi - lo

    # Full deliverables are generated from exactly the same scene used by the
    # reference render, so the preview and print mesh cannot drift apart.
    full_stl = OUT / "zhuisha_no1_v1.stl"
    write_binary_stl(full_stl, vertices, faces)
    scene.write_obj(str(OUT / "zhuisha_no1_v1.obj"), mtllib="zhuisha_no1_v1.mtl")
    (OUT / "zhuisha_no1_v1.mtl").write_text(M.MTL, encoding="utf-8")

    manifest = {
        "model": "Zhuisha No.1 exterior shell v1",
        "units": "mm",
        "coordinate_system": {"x": "nose to tail", "y": "starboard", "z": "up"},
        "bbox_mm": {"min": lo.tolist(), "max": hi.tolist(), "size": dims.tolist()},
        "scale_from_source": float(scale),
        "wall_thickness_assumption_mm": 3.2,
        "section_boundaries_mm": {"head": [0.0, 180.0], "body": [180.0, 520.0], "tail": [520.0, 710.0]},
        "source_faces": int(len(faces)),
        "files": {"full_stl": full_stl.name, "full_obj": "zhuisha_no1_v1.obj"},
        "notes": [
            "Section STL files are mesh cuts for import/reference; they are not yet water-tight CAD solids.",
            "The 0-120/120-300/300-420 dimensions belong to the older whale prompt and are not used here.",
        ],
    }

    centroids = vertices[faces].mean(axis=1)[:, 0]
    for name, x0, x1 in SEGMENTS:
        # Include a triangle when its centroid is in the section.  A small
        # overlap at the split planes avoids dropping interface details.
        keep = (centroids >= x0 - 0.5) & (centroids <= x1 + 0.5)
        sv, sf = compact_mesh(vertices, faces[keep])
        path = OUT / f"{name}_shell_mesh.stl"
        write_binary_stl(path, sv, sf)
        manifest.setdefault("segments", {})[name] = {
            "range_mm": [x0, x1],
            "vertices": int(len(sv)),
            "triangles": int(len(sf)),
            "file": path.name,
        }

    (OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print("[build] bbox mm: %.1f x %.1f x %.1f" % tuple(dims))
    print("[build] triangles: %d" % len(faces))
    print("[build] full: %s (%.2f MB)" % (full_stl, full_stl.stat().st_size / 1e6))
    for name, _, _ in SEGMENTS:
        item = manifest["segments"][name]
        print("[segment] %-4s triangles=%d vertices=%d -> %s" % (name, item["triangles"], item["vertices"], item["file"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
