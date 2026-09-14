#!/usr/bin/env python3
"""Assemble the installable addon from staged build cells."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from common import (
    ROOT,
    addon_version,
    compatibility_minimum,
    digest_of,
    fail,
    gdignore,
    git_head,
    load_json,
    main,
    read_pins,
    release_stem,
    run,
    write_json,
)
from matrix import Cell, load_catalog, resolve

ADDON = Path("addons") / "networked"
BIN = ADDON / "bin"
MANIFEST_NAME = "networked.gdextension"


def tracked_payload() -> list[Path]:
    """Every addon file the repository actually carries, as a closed list."""
    completed = run(
        ["git", "ls-files", "-z", "--", str(ADDON)],
        capture=True,
        timeout=120,
    )
    paths = [Path(entry) for entry in completed.stdout.split("\0") if entry]
    if not paths:
        raise fail("git lists no files under %s" % ADDON)
    return paths


def shipped_manifest() -> str:
    """The extension manifest with every path made relative to the addon root.

    Reloading is turned off for an installed addon. Reload fires on the
    editor's focus-in and rebinds live objects to a freshly built library,
    which is a development gesture; a consumer who deletes or replaces the
    addon has no new library to rebind to, and the editor is left holding
    instances whose code was unmapped.
    """
    text = (ROOT / "extension" / MANIFEST_NAME).read_text(encoding="utf-8")
    prefix = "res://%s/" % ADDON.as_posix()
    shipped = text.replace('"%s' % prefix, '"./')
    shipped = shipped.replace("reloadable = true", "reloadable = false")
    remaining = [line for line in shipped.splitlines() if "res://" in line]
    if remaining:
        raise fail("the shipped manifest still holds an absolute path: %s" % remaining[0].strip())
    if "reloadable = true" in shipped:
        raise fail("the shipped manifest still enables reloading")
    return shipped


def hide_staging(staging: Path) -> None:
    """Keep staged cells out of res://, since each carries its own manifest."""
    if not staging.exists():
        return
    gdignore(staging)
    resolved = staging.resolve()
    if resolved.parent != ROOT and ROOT in resolved.parents:
        gdignore(staging.parent)


def read_cell_record(staging: Path, cell: Cell, *, expect_sha: str | None) -> dict:
    directory = staging / cell.artifact
    record_path = directory / "build.json"
    if not record_path.is_file():
        raise fail("cell %s has no staged build record at %s" % (cell.id, record_path))
    record = load_json(record_path)
    if record["cell"]["id"] != cell.id:
        raise fail("%s holds a record for %s, not for %s" % (record_path, record["cell"]["id"], cell.id))
    if expect_sha and record["source"]["sha"] != expect_sha:
        raise fail(
            "cell %s was built at %s but the package is for %s"
            % (cell.id, record["source"]["sha"][:12], expect_sha[:12])
        )
    for relative in cell.outputs:
        if not (directory / relative).exists():
            raise fail("cell %s is missing its declared output %s" % (cell.id, relative))
    return record


def _apple(tool: str) -> str:
    if sys.platform != "darwin":
        raise fail(
            "%s runs on macOS only, so this package cannot be assembled here. "
            "Build the Apple cells on a Mac, or leave the platform out of the "
            "profile and record it unqualified." % tool
        )
    if not shutil.which(tool):
        raise fail("%s is not on PATH" % tool)
    return tool


def merge_framework(cells: list[Cell], staging: Path, destination: Path) -> str:
    """One universal framework from the per-architecture slices of a target."""
    name = cells[0].outputs[0]
    binary_name = name.removesuffix(".framework")
    slices = [staging / cell.artifact / name / binary_name for cell in cells]
    for path in slices:
        if not path.is_file():
            raise fail("framework slice %s is missing" % path)
    target = destination / name
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(staging / cells[0].artifact / name, target, symlinks=True)
    output = target / binary_name
    if len(slices) == 1:
        return name
    run([_apple("lipo"), "-create", *[str(p) for p in slices], "-output", str(output)], timeout=600)
    return name


def _lipo_or_single(paths: list[Path], output: Path) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    if len(paths) == 1:
        shutil.copy2(paths[0], output)
        return output
    run([_apple("lipo"), "-create", *[str(p) for p in paths], "-output", str(output)], timeout=600)
    return output


def assemble_xcframework(
    cells: list[Cell],
    staging: Path,
    destination: Path,
    work: Path,
) -> str:
    """One xcframework from the device slice and the fat simulator slice."""
    target = cells[0].target
    device = [cell for cell in cells if not cell.simulator]
    simulator = [cell for cell in cells if cell.simulator]
    if not device:
        raise fail("ios %s has no device slice" % target)
    if not simulator:
        raise fail("ios %s has no simulator slice" % target)

    name = "libnetworked.ios.%s.xcframework" % target
    output = destination / name
    if output.exists():
        shutil.rmtree(output)

    def staged(cell: Cell) -> Path:
        path = staging / cell.artifact / cell.outputs[0]
        if not path.is_file():
            raise fail("ios slice %s is missing" % path)
        return path

    device_binary = _lipo_or_single([staged(cell) for cell in device], work / target / "device" / "libnetworked.dylib")
    simulator_binary = _lipo_or_single(
        [staged(cell) for cell in simulator],
        work / target / "simulator" / "libnetworked.dylib",
    )
    run(
        [
            _apple("xcodebuild"),
            "-create-xcframework",
            "-library",
            str(device_binary),
            "-library",
            str(simulator_binary),
            "-output",
            str(output),
        ],
        timeout=900,
    )
    return name


def place_plain(cell: Cell, staging: Path, destination: Path, placed: dict[str, str]) -> list[str]:
    names = []
    for relative in cell.outputs:
        source = staging / cell.artifact / relative
        digest = digest_of(source)
        if relative in placed and placed[relative] != digest:
            raise fail("cell %s would overwrite %s with different content" % (cell.id, relative))
        target = destination / relative
        if source.is_dir():
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(source, target, symlinks=True)
        else:
            shutil.copy2(source, target)
        placed[relative] = digest
        names.append(relative)
    return names


def assemble(
    cells: list[Cell],
    staging: Path,
    stage_root: Path,
    *,
    expect_sha: str | None,
) -> dict:
    records = {cell.id: read_cell_record(staging, cell, expect_sha=expect_sha) for cell in cells}

    if stage_root.exists():
        shutil.rmtree(stage_root)
    gdignore(stage_root.parent)
    addon_root = stage_root / ADDON
    (addon_root / "bin").mkdir(parents=True)

    payload: dict[str, dict] = {}
    for relative in tracked_payload():
        source = ROOT / relative
        target = stage_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        payload[str(relative)] = {
            "sha256": digest_of(target),
            "bytes": target.stat().st_size,
        }

    (addon_root / MANIFEST_NAME).write_text(shipped_manifest(), encoding="utf-8")

    bin_dir = addon_root / "bin"
    work = stage_root.parent / "apple-work"
    placed: dict[str, str] = {}
    libraries: list[str] = []

    by_platform_target: dict[tuple[str, str], list[Cell]] = {}
    for cell in cells:
        by_platform_target.setdefault((cell.platform, cell.target), []).append(cell)

    for (platform, target), group in sorted(by_platform_target.items()):
        del target
        kind = group[0].package
        if kind == "framework":
            libraries.append(merge_framework(group, staging, bin_dir))
        elif kind == "xcframework":
            libraries.append(assemble_xcframework(group, staging, bin_dir, work))
        else:
            for cell in group:
                libraries.extend(place_plain(cell, staging, bin_dir, placed))
        del platform

    for name in sorted(libraries):
        entry = bin_dir / name
        payload[str(BIN / name)] = {
            "sha256": digest_of(entry),
            "bytes": (
                sum(f.stat().st_size for f in entry.rglob("*") if f.is_file())
                if entry.is_dir()
                else entry.stat().st_size
            ),
        }
    shipped = addon_root / MANIFEST_NAME
    payload[str(ADDON / MANIFEST_NAME)] = {
        "sha256": digest_of(shipped),
        "bytes": shipped.stat().st_size,
    }

    return {"records": records, "payload": payload, "libraries": sorted(libraries)}


def deploy(cells: list[Cell], staging: Path, bin_dir: Path, *, expect_sha: str | None) -> list[str]:
    """Put the named cells' libraries into an addon bin a job is about to load."""
    hide_staging(staging)
    for cell in cells:
        read_cell_record(staging, cell, expect_sha=expect_sha)
    bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "extension" / MANIFEST_NAME, bin_dir / MANIFEST_NAME)
    placed: dict[str, str] = {}
    names: list[str] = []
    for cell in cells:
        if cell.package == "framework":
            names.append(merge_framework([cell], staging, bin_dir))
        else:
            names.extend(place_plain(cell, staging, bin_dir, placed))
    for name in names:
        print("DEPLOYED %s" % name)
    return names


def release_manifest(
    cells: list[Cell],
    assembled: dict,
    *,
    version: str,
    sha: str,
    unqualified: list[str],
) -> dict:
    pins = read_pins()
    minimum = compatibility_minimum()
    return {
        "product": "networked",
        "version": version,
        "source": {"sha": sha},
        "godot": {
            "cpp_ref": pins["GODOT_CPP_REF"],
            "api_floor": pins["GODOT_CPP_API_VERSION"],
            "compatibility_minimum": minimum,
        },
        "cells": [
            {
                **cell.to_json(),
                "toolchain": assembled["records"][cell.id]["toolchain"],
                "files": assembled["records"][cell.id]["files"],
            }
            for cell in cells
        ],
        "libraries": assembled["libraries"],
        "payload": assembled["payload"],
        "unqualified": unqualified,
    }


def write_zip(stage_root: Path, archive: Path) -> None:
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(p for p in stage_root.rglob("*") if p.is_file()):
            bundle.write(path, path.relative_to(stage_root).as_posix())


def write_sums(paths: list[Path], destination: Path) -> None:
    lines = ["%s  %s" % (digest_of(path), path.name) for path in paths if path.exists()]
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", default="release")
    parser.add_argument("--cells", default="")
    parser.add_argument("--staging", type=Path, default=ROOT / "dist" / "cells")
    parser.add_argument("--out", type=Path, default=ROOT / "dist")
    parser.add_argument("--expect-sha", default="")
    parser.add_argument(
        "--unqualified",
        default="",
        help="Comma-separated platforms this run cannot qualify. They are named in the manifest.",
    )
    parser.add_argument(
        "--deploy-into",
        type=Path,
        help="Populate this addon bin from the staged cells instead of building a release.",
    )
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    catalog = load_catalog()
    from matrix import parse_list

    cells = resolve(
        catalog,
        profile=args.profile if not args.cells else None,
        cells=parse_list(args.cells),
    )
    sha = args.expect_sha or git_head()

    if args.deploy_into is not None:
        deploy(cells, args.staging, args.deploy_into, expect_sha=sha)
        return 0

    version = addon_version()
    stage_root = args.out / "stage"
    assembled = assemble(cells, args.staging, stage_root, expect_sha=sha)

    archive = args.out / ("%s.zip" % release_stem(version))
    write_zip(stage_root, archive)
    manifest_path = args.out / ("%s.manifest.json" % release_stem(version))
    write_json(
        manifest_path,
        release_manifest(
            cells,
            assembled,
            version=version,
            sha=sha,
            unqualified=parse_list(args.unqualified),
        ),
    )
    write_sums([archive, manifest_path], args.out / "SHA256SUMS.txt")

    print("packaged %s over %d cells" % (archive.name, len(cells)))
    print(json.dumps(assembled["libraries"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
