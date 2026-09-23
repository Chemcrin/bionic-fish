#!/usr/bin/env python3
"""Build through CMake; optionally provision pinned dependencies in a user cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "scripts/dependencies.lock.json").read_text(encoding="utf-8"))


def run(command, **kwargs):
    command = [str(value) for value in command]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
    if kwargs.get("capture_output"):
        return subprocess.run(command, check=True, **kwargs)
    # Explicit pipes preserve diagnostics for hidden Windows console processes.
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", **kwargs)
    if result.stdout:
        print(result.stdout, end="", flush=True)
    result.check_returncode()
    return result


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest() if sys.version_info >= (3, 11) else _hash(source)


def _hash(source):
    digest = hashlib.sha256()
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def cached_zip(cache, name):
    spec = LOCK["windows"][name]
    archive = cache / spec["archive"]
    if not archive.exists() or sha256(archive) != spec["sha256"]:
        print("Downloading " + spec["url"], flush=True)
        temporary = archive.with_suffix(archive.suffix + ".part")
        with urllib.request.urlopen(spec["url"], timeout=60) as source, temporary.open("wb") as target:
            shutil.copyfileobj(source, target)
        if sha256(temporary) != spec["sha256"]:
            raise RuntimeError("SHA-256 mismatch: " + str(temporary))
        temporary.replace(archive)
    destination = cache / spec["directory"]
    marker = destination / ".bionic-extracted"
    if not marker.exists():
        base = destination if name == "ninja" else cache
        base.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as package:
            for member in package.infolist():
                candidate = (base / member.filename).resolve()
                if not candidate.is_relative_to(base.resolve()):
                    raise RuntimeError("Unsafe archive member: " + member.filename)
            package.extractall(base)
        marker.write_text(spec["sha256"], encoding="ascii")
    return destination


def bootstrap_cube(cache):
    spec = LOCK["cube_f1"]
    cube = cache / ("STM32CubeF1-v" + spec["version"])
    if not cube.exists():
        run(["git", "-c", "core.longpaths=true", "clone", "--depth", "1", "--branch",
             "v" + spec["version"], "--filter=blob:none", "--sparse", spec["repository"], cube])
        run(["git", "-C", cube, "sparse-checkout", "set", "--skip-checks",
             "Drivers/CMSIS/Include", *spec["submodules"]])
    actual = run(["git", "-C", cube, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if actual != spec["commit"]:
        raise RuntimeError("Cached CubeF1 commit differs from dependencies.lock.json")
    run(["git", "-C", cube, "submodule", "update", "--init", "--depth", "1", *spec["submodules"]])
    for path, expected in spec["submodules"].items():
        actual = run(["git", "-C", cube / path, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
        if actual != expected:
            raise RuntimeError("CubeF1 submodule version mismatch: " + path)
    return cube


def executable(explicit, cached, name):
    candidate = explicit or (str(cached) if cached.is_file() else shutil.which(name))
    if not candidate:
        raise RuntimeError("Missing " + name + "; install the tool or supply its explicit path (see scripts/BUILDING.md).")
    return str(Path(candidate).resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=["firmware", "host"])
    parser.add_argument("--bootstrap", action="store_true", help="Fetch pinned CubeF1; on Windows also fetch CMake, Ninja and Arm GCC")
    parser.add_argument("--cache", type=Path, default=Path(os.environ.get("LOCALAPPDATA", Path.home() / ".cache")) / "bionic-fish-build")
    parser.add_argument("--cube-path", type=Path, help="STM32CubeF1 v1.8.6 package root")
    parser.add_argument("--toolchain-bin", type=Path)
    parser.add_argument("--cmake")
    parser.add_argument("--ninja")
    parser.add_argument("--host-cc", help="Native C compiler path; never the Arm cross compiler")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--config", default="MinSizeRel", choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"])
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
    args = parser.parse_args()
    cache = args.cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    cube = args.cube_path or cache / ("STM32CubeF1-v" + LOCK["cube_f1"]["version"])
    if args.bootstrap:
        if os.name == "nt":
            for name in ("cmake", "ninja") + (("arm_gcc",) if args.target == "firmware" else ()):
                cached_zip(cache, name)
        if args.target == "firmware" and args.cube_path is None:
            cube = bootstrap_cube(cache)
    suffix = ".exe" if os.name == "nt" else ""
    cmake = executable(args.cmake, cache / LOCK["windows"]["cmake"]["directory"] / "bin" / ("cmake" + suffix), "cmake")
    ninja = executable(args.ninja, cache / LOCK["windows"]["ninja"]["directory"] / ("ninja" + suffix), "ninja")
    build = (args.build_dir or ROOT / ("build-" + args.target)).resolve()
    env = os.environ.copy()
    tool_dirs = [str(Path(cmake).parent), str(Path(ninja).parent)]
    configure = [cmake, "-S", ROOT, "-B", build, "-G", "Ninja",
                 "-DCMAKE_MAKE_PROGRAM=" + Path(ninja).as_posix(), "-DCMAKE_BUILD_TYPE=" + args.config]
    if args.target == "firmware":
        arm_bin = args.toolchain_bin or cache / LOCK["windows"]["arm_gcc"]["directory"] / "bin"
        arm_gcc = executable(None, arm_bin / ("arm-none-eabi-gcc" + suffix), "arm-none-eabi-gcc")
        tool_dirs.append(str(Path(arm_gcc).parent))
        configure += ["-DCMAKE_TOOLCHAIN_FILE=" + (ROOT / "cmake/arm-none-eabi-gcc.cmake").as_posix(),
                      "-DARM_TOOLCHAIN_BIN=" + Path(arm_gcc).parent.as_posix(),
                      "-DSTM32CUBE_F1_PATH=" + cube.resolve().as_posix(),
                      "-DBIONIC_FISH_BUILD_FIRMWARE=ON", "-DBIONIC_FISH_BUILD_HOST_TESTS=OFF",
                      "-DBIONIC_FISH_WERROR=ON"]
    else:
        compiler = args.host_cc or shutil.which("gcc") or shutil.which("clang")
        if compiler:
            compiler = str(Path(compiler).resolve())
            tool_dirs.append(str(Path(compiler).parent))
            configure += ["-DCMAKE_C_COMPILER=" + Path(compiler).as_posix()]
        configure += ["-DBIONIC_FISH_BUILD_FIRMWARE=OFF", "-DBIONIC_FISH_BUILD_HOST_TESTS=ON"]
    env["PATH"] = os.pathsep.join(tool_dirs + [env.get("PATH", "")])
    run(configure, env=env)
    run([cmake, "--build", build, "--parallel", args.jobs], env=env)
    if args.target == "host":
        ctest = Path(cmake).with_name("ctest" + suffix)
        run([ctest, "--test-dir", build, "--output-on-failure"], env=env)
    else:
        files = {}
        for extension in ("elf", "bin", "map"):
            path = build / ("bionic_fish." + extension)
            files[path.name] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
        info = {"configuration": args.config, "cube_f1_expected": LOCK["cube_f1"],
                "cube_f1_path": str(cube.resolve()), "files": files}
        info["compiler"] = run([arm_gcc, "--version"], capture_output=True, text=True, env=env).stdout.splitlines()[0]
        info["cmake"] = run([cmake, "--version"], capture_output=True, text=True, env=env).stdout.splitlines()[0]
        info["source_commit"] = run(["git", "-C", ROOT, "rev-parse", "HEAD"], capture_output=True, text=True, env=env).stdout.strip()
        info["source_dirty"] = bool(run(["git", "-C", ROOT, "status", "--porcelain", "--", "."], capture_output=True, text=True, env=env).stdout.strip())
        (build / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(files, indent=2), flush=True)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print("Build failed: " + str(error), file=sys.stderr)
        sys.exit(1)
