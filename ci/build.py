#!/usr/bin/env python3
"""Build one library cell and record exactly what it produced."""

from __future__ import annotations

import argparse
import os
import platform as host_platform
import shutil
import sys
import time
from pathlib import Path

from common import (
    ROOT,
    digest_of,
    fail,
    gdignore,
    git_dirty,
    git_head,
    main,
    read_pins,
    run,
    write_json,
)
from matrix import Cell, load_catalog, resolve

BIN_DIR = ROOT / "addons" / "networked" / "bin"
MANIFEST_NAME = "networked.gdextension"


def _probe_all(argv: list[str]) -> str:
    if not shutil.which(argv[0]):
        return ""
    completed = run(argv, capture=True, check=False, timeout=60)
    if completed.returncode != 0:
        return ""
    return (completed.stdout or completed.stderr or "").strip()


def _probe(argv: list[str]) -> str:
    text = _probe_all(argv)
    return text.splitlines()[0] if text else ""


def _scons_version() -> str:
    """The line of `scons --version` that carries the version."""
    for line in _probe_all(["scons", "--version"]).splitlines():
        stripped = line.strip()
        if stripped.startswith("SCons: v"):
            return stripped.split("v", 1)[1].split(",", 1)[0]
    return ""


def _producer(path: Path) -> str:
    """The compiler string the object file carries, when it carries one."""
    if path.is_dir() or not shutil.which("readelf"):
        return ""
    completed = run(
        ["readelf", "-p", ".comment", str(path)],
        capture=True,
        check=False,
        timeout=60,
    )
    if completed.returncode != 0:
        return ""
    stamps = [
        line.split("]", 1)[1].strip()
        for line in completed.stdout.splitlines()
        if "]" in line and line.strip().startswith("[")
    ]
    return "; ".join(sorted(set(stamps)))


def toolchain_record(cell: Cell, pins: dict[str, str]) -> dict:
    record = {
        "host": host_platform.platform(),
        "host_machine": host_platform.machine(),
        "python": sys.version.split()[0],
        "scons": _scons_version(),
        "scons_pin": pins["SCONS_VERSION"],
        "godot_cpp_ref": pins["GODOT_CPP_REF"],
        "godot_cpp_api_version": pins["GODOT_CPP_API_VERSION"],
    }
    if cell.platform == "web":
        record["emscripten"] = _probe(["emcc", "--version"])
        record["emscripten_pin"] = pins["EM_VERSION"]
    if cell.platform == "android":
        record["android_ndk_root"] = os.environ.get("ANDROID_NDK_ROOT", "")
    if cell.platform in ("macos", "ios"):
        record["xcode"] = _probe(["xcodebuild", "-version"])
    return record


def scons_argv(cell: Cell, *, jobs: int, extra: list[str]) -> list[str]:
    """The cell's arguments, then the shipping defaults an extra may override."""
    overridden = {arg.split("=", 1)[0] for arg in extra if "=" in arg}
    defaults = [arg for arg in ("netw_tests=no", "netw_profiling=no") if arg.split("=")[0] not in overridden]
    if cell.platform == "android" and "ndk_version" not in overridden:
        defaults.append("ndk_version=%s" % read_pins()["ANDROID_NDK_VERSION"])
    argv = ["scons", "-C", str(ROOT / "extension"), "-j%d" % jobs]
    argv.extend(cell.scons_args)
    argv.extend(defaults)
    argv.extend(extra)
    return argv


def verify_outputs(cell: Cell, started: float, *, require_fresh: bool) -> list[Path]:
    """The declared outputs, on disk, and written by this run when asked.

    Existence is the gate that catches a cache convincing SCons there is
    nothing to do. Freshness is the stricter question a release candidate
    asks, where a library carried over from an earlier configuration would
    be packaged as though this run had produced it.
    """
    produced: list[Path] = []
    for relative in cell.outputs:
        path = BIN_DIR / relative
        if not path.exists():
            raise fail("%s produced no %s" % (cell.id, relative))
        newest = path.stat().st_mtime
        if path.is_dir():
            newest = max(
                [child.stat().st_mtime for child in path.rglob("*") if child.is_file()],
                default=0.0,
            )
        if newest < started:
            message = "%s left %s untouched, so this run reused an existing library" % (cell.id, relative)
            if require_fresh:
                raise fail(message)
            print("::warning::%s" % message)
        produced.append(path)
    return produced


def strip_outputs(cell: Cell, produced: list[Path]) -> str:
    """Remove debug symbols with the toolchain that produced the file."""
    if cell.strip == "none":
        return "skipped"
    if cell.strip == "elf":
        tool = "strip"
        argv = ["--strip-unneeded"]
    else:
        ndk = os.environ.get("ANDROID_NDK_ROOT", "")
        if not ndk:
            raise fail("ANDROID_NDK_ROOT is unset, so %s cannot be stripped" % cell.id)
        matches = sorted(Path(ndk).glob("toolchains/llvm/prebuilt/*/bin/llvm-strip"))
        if not matches:
            raise fail("no llvm-strip under %s" % ndk)
        tool = str(matches[0])
        argv = ["--strip-unneeded"]
    if not shutil.which(tool) and not Path(tool).is_file():
        raise fail("%s is not available, so %s cannot be stripped" % (tool, cell.id))
    for path in produced:
        if path.is_file():
            run([tool] + argv + [str(path)], timeout=300)
    return tool


def stage(cell: Cell, produced: list[Path], staging: Path) -> list[str]:
    gdignore(staging)
    if staging.resolve().parent != ROOT and ROOT in staging.resolve().parents:
        gdignore(staging.parent)
    destination = staging / cell.artifact
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    staged: list[str] = []
    for path in produced:
        target = destination / path.name
        if path.is_dir():
            shutil.copytree(path, target, symlinks=True)
        else:
            shutil.copy2(path, target)
        staged.append(path.name)
    manifest = ROOT / "extension" / MANIFEST_NAME
    shutil.copy2(manifest, destination / MANIFEST_NAME)
    staged.append(MANIFEST_NAME)
    return staged


def build_cell(
    cell: Cell,
    *,
    jobs: int,
    staging: Path | None,
    extra: list[str],
    timeout: float,
    strip: bool = False,
    require_fresh: bool = False,
) -> dict:
    pins = read_pins()
    started = time.time()
    argv = scons_argv(cell, jobs=jobs, extra=extra)
    run(argv, timeout=timeout)
    produced = verify_outputs(cell, started, require_fresh=require_fresh)
    stripped = strip_outputs(cell, produced) if strip else "not requested"
    record = {
        "stripped": stripped,
        "cell": cell.to_json(),
        "source": {"sha": git_head(), "dirty": git_dirty()},
        "toolchain": toolchain_record(cell, pins),
        "command": argv,
        "files": {
            path.name: {
                "sha256": digest_of(path),
                "bytes": (
                    sum(f.stat().st_size for f in path.rglob("*") if f.is_file())
                    if path.is_dir()
                    else path.stat().st_size
                ),
                "producer": _producer(path),
            }
            for path in produced
        },
        "seconds": round(time.time() - started, 1),
    }
    if staging:
        record["staged"] = stage(cell, produced, staging)
        write_json(staging / cell.artifact / "build.json", record)
    return record


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cell", help="platform:slice:target")
    parser.add_argument("--profile")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--staging", type=Path, default=ROOT / "dist" / "cells")
    parser.add_argument("--no-staging", action="store_true")
    parser.add_argument("--timeout", type=float, default=3600.0)
    parser.add_argument("--strip", action="store_true", help="Strip with this platform's toolchain.")
    parser.add_argument(
        "--require-fresh",
        action="store_true",
        help="Refuse an output this run did not write, which a release candidate must.",
    )
    parser.add_argument("--record", type=Path)
    parser.add_argument(
        "--scons-arg",
        action="append",
        default=[],
        dest="extra",
        help="An extra key=value passed through to SCons.",
    )
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if bool(args.cell) == bool(args.profile):
        raise fail("pass exactly one of --cell or --profile")
    catalog = load_catalog()
    cells = resolve(
        catalog,
        profile=args.profile,
        cells=[args.cell] if args.cell else None,
    )
    staging = None if args.no_staging else args.staging
    records = [
        build_cell(
            cell,
            jobs=args.jobs,
            staging=staging,
            extra=args.extra,
            timeout=args.timeout,
            strip=args.strip,
            require_fresh=args.require_fresh,
        )
        for cell in cells
    ]
    if args.record:
        write_json(args.record, {"cells": records})
    for record in records:
        print(
            "built %s in %ss: %s"
            % (
                record["cell"]["id"],
                record["seconds"],
                ", ".join(sorted(record["files"])),
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
