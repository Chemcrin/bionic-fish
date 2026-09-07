"""Export the validated Zhuisha mesh as a faceted AP214 STEP file.

This is intentionally dependency-free.  It writes a STEP faceted B-REP with
one planar face per source triangle, so SolidWorks/Fusion/FreeCAD can consume
the result even when the SolidWorks desktop application is unavailable.  It
is a reference/transfer model, not a parametric NURBS reconstruction.
"""
from __future__ import annotations

import datetime as dt
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "source"
OUT = HERE / "out"
sys.path.insert(0, str(SOURCE))

import model as M  # noqa: E402


def _fmt(v: float) -> str:
    """Stable STEP real formatting without scientific notation."""
    return "%.9f" % float(v)


class EntityWriter:
    def __init__(self):
        self.lines: list[str] = []
        self.next_id = 1

    def add(self, text: str) -> int:
        ident = self.next_id
        self.next_id += 1
        self.lines.append("#%d=%s;" % (ident, text))
        return ident


def _context(w: EntityWriter) -> int:
    app = w.add("APPLICATION_CONTEXT('core data for automotive mechanical design processes')")
    w.add("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#%d)" % app)
    length = w.add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    angle = w.add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    solid = w.add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    uncertainty = w.add(
        "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-6),#%d,'distance_accuracy_value','confusion accuracy')"
        % length
    )
    return w.add(
        "( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d)) "
        "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d)) REPRESENTATION_CONTEXT('3D SPACE','') )"
        % (uncertainty, length, angle, solid)
    )


def _product_header(w: EntityWriter, name: str):
    app = 1  # APPLICATION_CONTEXT is always written first below
    # The product graph is written after the application context.  IDs are
    # allocated normally, then the shape representation is linked at the end.
    product_context = w.add("PRODUCT_CONTEXT('',#%d,'mechanical')" % app)
    formation = w.add("PRODUCT_DEFINITION_FORMATION('1','v1',#%d)" % (product_context + 1))
    # product id is created next; patching by predictable allocation keeps the
    # output easy to inspect and avoids a second pass over the large face set.
    product = w.add("PRODUCT('%s','%s','',(#%d))" % (name, name, product_context))
    # Formation above points to the product context only as a placeholder;
    # replace it with the actual product id before writing the file.
    w.lines[formation - 1] = "#%d=PRODUCT_DEFINITION_FORMATION('1','v1',#%d);" % (formation, product)
    definition_context = w.add("PRODUCT_DEFINITION_CONTEXT('part definition',#%d,'design')" % app)
    definition = w.add("PRODUCT_DEFINITION('design','%s',#%d,#%d)" % (name, formation, definition_context))
    shape = w.add("PRODUCT_DEFINITION_SHAPE('','',#%d)" % definition)
    return product, shape


def write_step(path: Path, vertices: np.ndarray, faces: np.ndarray, name: str) -> dict:
    """Write one faceted STEP product and return file statistics."""
    w = EntityWriter()
    # Reserve the application context as entity #1 to keep the product graph
    # conventional and compatible with older STEP readers.
    w.add("APPLICATION_CONTEXT('core data for automotive mechanical design processes')")
    w.add("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#1)")
    length = w.add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    angle = w.add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    solid = w.add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    uncertainty = w.add(
        "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-6),#%d,'distance_accuracy_value','confusion accuracy')"
        % length
    )
    context = w.add(
        "( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d)) "
        "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d)) REPRESENTATION_CONTEXT('3D SPACE','') )"
        % (uncertainty, length, angle, solid)
    )

    product_context = w.add("PRODUCT_CONTEXT('',#1,'mechanical')")
    product = w.add("PRODUCT('%s','%s','',(#%d))" % (name, name, product_context))
    formation = w.add("PRODUCT_DEFINITION_FORMATION('1','v1',#%d)" % product)
    definition_context = w.add("PRODUCT_DEFINITION_CONTEXT('part definition',#1,'design')")
    definition = w.add("PRODUCT_DEFINITION('design','%s',#%d,#%d)" % (name, formation, definition_context))
    shape = w.add("PRODUCT_DEFINITION_SHAPE('','',#%d)" % definition)

    point_ids = []
    for p in vertices:
        point_ids.append(w.add("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(_fmt(x) for x in p)))

    face_ids = []
    tri = vertices[faces]
    normals = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    valid = lengths > 1.0e-10
    for f, n, ln in zip(faces[valid], normals[valid], lengths[valid]):
        normal = n / ln
        tangent = vertices[f[1]] - vertices[f[0]]
        tangent = tangent - normal * np.dot(tangent, normal)
        tl = np.linalg.norm(tangent)
        if tl < 1.0e-10:
            continue
        tangent /= tl
        origin = point_ids[f[0]]
        ndir = w.add("DIRECTION('',(%s,%s,%s))" % tuple(_fmt(x) for x in normal))
        xdir = w.add("DIRECTION('',(%s,%s,%s))" % tuple(_fmt(x) for x in tangent))
        axis = w.add("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (origin, ndir, xdir))
        plane = w.add("PLANE('',#%d)" % axis)
        loop = w.add("POLY_LOOP('',(#%d,#%d,#%d))" % tuple(point_ids[int(i)] for i in f))
        bound = w.add("FACE_OUTER_BOUND('',#%d,.T.)" % loop)
        face_ids.append(w.add("ADVANCED_FACE('',(#%d),#%d,.T.)" % (bound, plane)))

    shell = w.add("CLOSED_SHELL('',(%s))" % ",".join("#%d" % i for i in face_ids))
    brep = w.add("FACETED_BREP('%s',#%d)" % (name, shell))
    representation = w.add("ADVANCED_BREP_SHAPE_REPRESENTATION('',(#%d),#%d)" % (brep, context))
    w.add("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (shape, representation))
    w.add("PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#%d))" % product)

    stamp = dt.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
    header = [
        "ISO-10303-21;",
        "HEADER;",
        "FILE_DESCRIPTION(('OpenAI faceted STEP export'),'2;1');",
        "FILE_NAME('%s','%s',('OpenAI'),(''),'mesh_to_step.py','OpenAI','faceted mesh');" % (path.name, stamp),
        "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));",
        "ENDSEC;",
        "DATA;",
    ]
    path.write_text("\n".join(header + w.lines + ["ENDSEC;", "END-ISO-10303-21;", ""]), encoding="ascii")
    return {"file": path.name, "vertices": int(len(vertices)), "triangles": int(len(face_ids)), "bytes": path.stat().st_size}


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    scene, _ = M.build()
    vertices, faces, _, _ = scene.merged()
    centroid_x = vertices[faces].mean(axis=1)[:, 0]
    outputs = {}
    outputs["full"] = write_step(OUT / "zhuisha_no1_v1.step", vertices, faces, "zhuisha_no1_v1")
    # Keep an explicit assembly-name deliverable for downstream scripts and
    # SolidWorks import dialogs.  It carries the complete positioned exterior
    # as one faceted product; the separate section STEP files remain available
    # when a true multi-component assembly is needed.
    outputs["assembly_reference"] = write_step(
        OUT / "Zhuisha_No1_Assembly.step", vertices, faces, "Zhuisha_No1_Assembly"
    )
    for name, x0, x1 in (("head", 0.0, 180.0), ("body", 180.0, 520.0), ("tail", 520.0, 710.0)):
        keep = (centroid_x >= x0 - 0.5) & (centroid_x <= x1 + 0.5)
        used = np.unique(faces[keep].reshape(-1))
        remap = np.full(len(vertices), -1, dtype=np.int64)
        remap[used] = np.arange(len(used))
        outputs[name] = write_step(OUT / (name + "_shell.step"), vertices[used], remap[faces[keep]], "zhuisha_no1_" + name)
    manifest_path = OUT / "step_manifest.json"
    manifest_path.write_text(json.dumps(outputs, ensure_ascii=False, indent=2), encoding="utf-8")
    for item in outputs.values():
        print("[step] %-20s triangles=%d bytes=%.2f MB" % (item["file"], item["triangles"], item["bytes"] / 1e6))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
