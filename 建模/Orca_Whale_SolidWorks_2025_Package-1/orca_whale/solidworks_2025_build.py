#!/usr/bin/env python3
"""Import the generated orca STEP B-reps into SOLIDWORKS 2025.

Run this file on 64-bit Windows with SOLIDWORKS 2025 and pywin32 installed.
It writes exactly:

    native/Head.SLDPRT
    native/Body.SLDPRT
    native/Tail.SLDPRT
    native/Whale.sldprt          (multi-body master part)
    native/Whale.SLDASM
    native/Whale_Assembly.STEP

The STEP geometry uses millimetres.  All numeric coordinates passed to the
SOLIDWORKS COM API use metres through mm2m(), as required by the API.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


# swconst values used in this script.  Named constants from the installed type
# library are preferred where a local preference enum is needed.
SW_DOC_PART = 1
SW_OPEN_SILENT = 1
SW_SOLID_BODY = 0
SW_SHEET_BODY = 1
SW_SAVE_CURRENT_VERSION = 0
SW_SAVE_SILENT = 1
SW_ADD_CURRENT_SELECTED_CONFIG = 0
SW_BODY_ADD = 15903
SW_DEFAULT_TEMPLATE_ASSEMBLY = 9


def mm2m(value_mm: float) -> float:
    """Convert millimetres to SOLIDWORKS API base units (metres)."""
    return float(value_mm) / 1000.0


class BuildError(RuntimeError):
    pass


def require_windows() -> None:
    if platform.system() != "Windows":
        raise BuildError(
            "Native SLDPRT generation requires 64-bit Windows with a licensed "
            "SOLIDWORKS 2025 COM server. Generate STEP/STL here with "
            "generate_whale_geometry.py, then run this script on that machine."
        )


def byref_i4(pythoncom: Any, initial: int = 0) -> Any:
    from win32com.client import VARIANT

    return VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, initial)


def null_dispatch(pythoncom: Any) -> Any:
    from win32com.client import VARIANT

    return VARIANT(pythoncom.VT_DISPATCH, None)


def connect_solidworks(visible: bool) -> tuple[Any, Any, Any, Any]:
    require_windows()
    try:
        import pythoncom
        import win32com.client
    except ImportError as exc:
        raise BuildError("pywin32 is required: py -m pip install pywin32") from exc

    pythoncom.CoInitialize()
    try:
        # Force a late-bound wrapper even when this machine already has a makepy
        # cache; explicit BYREF VARIANTs are used for SOLIDWORKS [out] LONGs.
        initial = win32com.client.Dispatch("SldWorks.Application")
        sw = win32com.client.dynamic.DumbDispatch(
            initial._oleobj_, "SldWorks.Application"
        )
    except Exception as exc:
        raise BuildError(
            "Could not start SldWorks.Application. Confirm SOLIDWORKS 2025 is "
            "installed, activated, and registered for this Windows account."
        ) from exc
    sw.Visible = bool(visible)
    sw.UserControl = True
    revision = str(sw.RevisionNumber())
    # SOLIDWORKS 2025 reports major revision 33.  Warn rather than hiding a
    # version mismatch because forward/backward saves can alter native files.
    if revision and not revision.startswith("33."):
        print(f"WARNING: expected SOLIDWORKS 2025 (33.x); COM reports {revision}", file=sys.stderr)
    return sw, pythoncom, win32com.client, win32com.client.constants


def activate(sw: Any, pythoncom: Any, model: Any) -> None:
    active = sw.ActiveDoc
    target_type = int(model.GetType())
    target_path = str(model.GetPathName())
    if active is not None and int(active.GetType()) == target_type:
        active_path = str(active.GetPathName())
        if target_path and os.path.normcase(active_path) == os.path.normcase(target_path):
            return
        if not target_path and str(active.GetTitle()) == str(model.GetTitle()):
            return

    errors = byref_i4(pythoncom)
    title = str(model.GetTitle())
    activated = sw.ActivateDoc3(title, True, 1, errors)
    if activated is None:
        raise BuildError(f"Could not activate {title!r}; error={errors.value}")
    if int(activated.GetType()) != target_type:
        raise BuildError(
            f"ActivateDoc3({title!r}) activated the wrong document type"
        )
    if target_path:
        activated_path = str(activated.GetPathName())
        if os.path.normcase(activated_path) != os.path.normcase(target_path):
            raise BuildError(
                f"ActivateDoc3({title!r}) activated {activated_path!r}, not {target_path!r}"
            )
    if int(errors.value) != 0:
        print(f"WARNING: ActivateDoc3({title!r}) returned {errors.value}", file=sys.stderr)


def load_step_as_part(sw: Any, pythoncom: Any, step_path: Path) -> Any:
    if not step_path.is_file():
        raise BuildError(f"Missing STEP source: {step_path}")
    errors = byref_i4(pythoncom)
    import_data = sw.GetImportFileData(str(step_path))
    model = None
    if import_data is not None:
        # The 2025 API documents GetImportFileData + LoadFile4 for STEP.  The
        # property is optional across service packs, so only set it if exposed.
        if hasattr(import_data, "MapConfigurationData"):
            import_data.MapConfigurationData = False
        model = sw.LoadFile4(str(step_path), "r", import_data, errors)
    if model is None:
        warnings = byref_i4(pythoncom)
        model = sw.OpenDoc6(
            str(step_path), SW_DOC_PART, SW_OPEN_SILENT, "", errors, warnings
        )
    if model is None or int(errors.value) != 0:
        raise BuildError(f"STEP import failed for {step_path.name}; error={errors.value}")
    model.ForceRebuild3(False)
    return model


def as_sequence(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, (tuple, list)):
        return list(value)
    return [value]


def select_bodies_with_selectbyid2(model: Any, pythoncom: Any) -> dict[str, int]:
    """Exercise SelectByID2 and confirm imported bodies are selectable.

    SaveAs3 exports the whole model only with an empty selection, so this helper
    deliberately clears the selection after validation.
    """
    solid_bodies = as_sequence(model.GetBodies2(SW_SOLID_BODY, True))
    sheet_bodies = as_sequence(model.GetBodies2(SW_SHEET_BODY, True))
    if not solid_bodies:
        raise BuildError(f"{model.GetTitle()}: no solid bodies after STEP import")
    if sheet_bodies:
        raise BuildError(
            f"{model.GetTitle()}: STEP import produced {len(sheet_bodies)} sheet bodies"
        )

    selected = 0
    callout = null_dispatch(pythoncom)
    typed_bodies = [
        *((body, "SOLIDBODY") for body in solid_bodies),
        *((body, "SURFACEBODY") for body in sheet_bodies),
    ]
    for body, body_type in typed_bodies:
        body_name = str(body.Name)
        ok = model.Extension.SelectByID2(
            body_name,
            body_type,
            mm2m(0.0),
            mm2m(0.0),
            mm2m(0.0),
            selected > 0,
            0,
            callout,
            0,
        )
        if not ok:
            # Imported body naming can vary; Select2 still proves the COM body
            # is selectable, but failure is surfaced instead of silently ignored.
            if not body.Select2(selected > 0, callout):
                raise BuildError(f"Could not select imported body {body_name!r}")
        selected += 1
    model.ClearSelection2(True)
    return {"solid_bodies": len(solid_bodies), "sheet_bodies": len(sheet_bodies)}


def combine_all_solid_bodies(model: Any, constants: Any) -> dict[str, int]:
    """Boolean-union all intersecting imported bodies into one production body."""
    bodies = as_sequence(model.GetBodies2(SW_SOLID_BODY, True))
    if not bodies:
        raise BuildError(f"{model.GetTitle()}: no solids available for Combine")
    if len(bodies) == 1:
        return {"before": 1, "after": 1}

    import pythoncom

    selection_data = null_dispatch(pythoncom)
    model.ClearSelection2(True)
    for body in bodies:
        if not body.Select2(True, selection_data):
            raise BuildError(f"{model.GetTitle()}: could not select a body for Combine")
    operation = int(getattr(constants, "SWBODYADD", SW_BODY_ADD))
    feature = model.FeatureManager.InsertCombineFeature(
        operation, null_dispatch(pythoncom), null_dispatch(pythoncom)
    )
    model.ClearSelection2(True)
    if feature is None:
        raise BuildError(
            f"{model.GetTitle()}: Combine failed; imported bodies do not form one valid solid"
        )
    model.EditRebuild3()
    after = as_sequence(model.GetBodies2(SW_SOLID_BODY, True))
    sheets = as_sequence(model.GetBodies2(SW_SHEET_BODY, True))
    if len(after) != 1 or sheets:
        raise BuildError(
            f"{model.GetTitle()}: Combine result has {len(after)} solids and {len(sheets)} sheets"
        )
    return {"before": len(bodies), "after": len(after)}


def save_as3(sw: Any, pythoncom: Any, model: Any, destination: Path) -> dict[str, int | str]:
    destination.parent.mkdir(parents=True, exist_ok=True)
    activate(sw, pythoncom, model)
    model.EditRebuild3()
    # Official behavior: selected faces/bodies can restrict neutral-file export.
    # SelectByID2 is used for construction/validation above, then cleared here.
    model.ClearSelection2(True)
    errors = byref_i4(pythoncom)
    warnings = byref_i4(pythoncom)
    ok = model.Extension.SaveAs3(
        str(destination),
        SW_SAVE_CURRENT_VERSION,
        SW_SAVE_SILENT,
        null_dispatch(pythoncom),
        null_dispatch(pythoncom),
        errors,
        warnings,
    )
    error_value = int(errors.value)
    warning_value = int(warnings.value)
    if not ok or error_value != 0:
        raise BuildError(
            f"SaveAs3 failed for {destination.name}; ok={ok}, "
            f"errors={error_value}, warnings={warning_value}"
        )
    if not destination.is_file() or destination.stat().st_size == 0:
        raise BuildError(f"SaveAs3 returned success but {destination} is missing/empty")
    return {
        "file": str(destination),
        "bytes": destination.stat().st_size,
        "errors": error_value,
        "warnings": warning_value,
    }


def import_and_save_part(
    sw: Any,
    pythoncom: Any,
    source: Path,
    destination: Path,
    constants: Any,
    *,
    combine: bool,
) -> tuple[Any, dict[str, Any]]:
    model = load_step_as_part(sw, pythoncom, source)
    body_counts = select_bodies_with_selectbyid2(model, pythoncom)
    combine_counts = combine_all_solid_bodies(model, constants) if combine else None
    result = save_as3(sw, pythoncom, model, destination)
    result["source"] = str(source)
    result["body_counts"] = body_counts
    result["combine_counts"] = combine_counts
    return model, result


def default_assembly_template(sw: Any, constants: Any, explicit: Path | None) -> Path:
    if explicit is not None:
        template = explicit
    else:
        preference = int(
            getattr(constants, "swDefaultTemplateAssembly", SW_DEFAULT_TEMPLATE_ASSEMBLY)
        )
        template = Path(str(sw.GetUserPreferenceStringValue(preference)))
    if not template.is_file():
        raise BuildError(
            f"Assembly template not found: {template}. Configure the default "
            "SOLIDWORKS assembly template or pass --assembly-template."
        )
    return template


def create_assembly(
    sw: Any,
    pythoncom: Any,
    constants: Any,
    part_models: dict[str, Any],
    part_paths: dict[str, Path],
    native_dir: Path,
    assembly_template: Path | None,
) -> list[dict[str, Any]]:
    template = default_assembly_template(sw, constants, assembly_template)
    assembly_model = sw.NewDocument(str(template), 0, mm2m(0.0), mm2m(0.0))
    if assembly_model is None:
        raise BuildError(f"NewDocument failed with assembly template {template}")
    assembly = assembly_model

    components = []
    for name in ("Head", "Body", "Tail"):
        # AddComponent5 expects the component document to be loaded; keep the
        # part_models references alive and insert all at the shared global origin.
        if part_models[name] is None:
            raise BuildError(f"{name} part is not loaded")
        component = assembly.AddComponent5(
            str(part_paths[name]),
            SW_ADD_CURRENT_SELECTED_CONFIG,
            "",
            False,
            "",
            mm2m(0.0),
            mm2m(0.0),
            mm2m(0.0),
        )
        if component is None:
            raise BuildError(f"AddComponent5 failed for {part_paths[name].name}")
        components.append(component)

    assembly_model.EditRebuild3()
    # Verify all components through the requested SelectByID2 API.  Clear the
    # selection before saving the complete assembly/STEP.
    for index, component in enumerate(components):
        component_name = str(component.Name2)
        if hasattr(component, "GetSelectByIDString"):
            full_selection_name = str(component.GetSelectByIDString())
        else:
            full_selection_name = component_name
        callout = null_dispatch(pythoncom)
        selected = assembly_model.Extension.SelectByID2(
            full_selection_name,
            "COMPONENT",
            mm2m(0.0),
            mm2m(0.0),
            mm2m(0.0),
            index > 0,
            0,
            callout,
            0,
        )
        if not selected and not component.Select4(index > 0, callout, False):
            raise BuildError(f"Could not select component {component_name!r}")
    assembly_model.ClearSelection2(True)

    results = [
        save_as3(sw, pythoncom, assembly_model, native_dir / "Whale.SLDASM"),
        save_as3(sw, pythoncom, assembly_model, native_dir / "Whale_Assembly.STEP"),
    ]
    return results


def run(args: argparse.Namespace) -> dict[str, Any]:
    package_dir = args.package_dir.resolve()
    step_dir = package_dir / "output" / "STEP"
    native_dir = args.native_dir.resolve() if args.native_dir else package_dir / "native"
    native_dir.mkdir(parents=True, exist_ok=True)

    sw, pythoncom, _client, constants = connect_solidworks(args.visible)
    log: dict[str, Any] = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "solidworks_revision": str(sw.RevisionNumber()),
        "source_step_dir": str(step_dir),
        "outputs": [],
    }

    part_models: dict[str, Any] = {}
    part_paths: dict[str, Path] = {}
    for name in ("Head", "Body", "Tail"):
        destination = native_dir / f"{name}.SLDPRT"
        model, result = import_and_save_part(
            sw,
            pythoncom,
            step_dir / f"{name}.step",
            destination,
            constants,
            combine=True,
        )
        part_models[name] = model
        part_paths[name] = destination
        log["outputs"].append(result)

    _master, master_result = import_and_save_part(
        sw,
        pythoncom,
        step_dir / "Whale.step",
        native_dir / "Whale.sldprt",
        constants,
        combine=False,
    )
    log["outputs"].append(master_result)
    log["outputs"].extend(
        create_assembly(
            sw,
            pythoncom,
            constants,
            part_models,
            part_paths,
            native_dir,
            args.assembly_template.resolve() if args.assembly_template else None,
        )
    )
    log["completed_utc"] = datetime.now(timezone.utc).isoformat()
    log_path = native_dir / "solidworks_build_log.json"
    log_path.write_text(json.dumps(log, indent=2), encoding="utf-8")
    return log


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package-dir",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="Directory containing output/STEP",
    )
    parser.add_argument("--native-dir", type=Path, help="Native output directory")
    parser.add_argument("--assembly-template", type=Path, help="Path to Assembly.asmdot")
    parser.add_argument(
        "--hidden",
        dest="visible",
        action="store_false",
        help="Run SOLIDWORKS without showing its window",
    )
    parser.set_defaults(visible=True)
    args = parser.parse_args()
    try:
        result = run(args)
    except BuildError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
