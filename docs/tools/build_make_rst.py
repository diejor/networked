#!/usr/bin/env python3
"""Rebuild ``tools/make_rst.py`` from the pristine vendor copy and the patches.

Usage::

    python tools/build_make_rst.py            # rebuild
    python tools/build_make_rst.py --check    # verify the working copy is up-to-date

The vendor file (``tools/vendor/make_rst.py``) is a pristine copy of
https://raw.githubusercontent.com/godotengine/godot/refs/heads/4.6/doc/tools/make_rst.py.
All Networked-specific modifications live in ``tools/patches/*.patch`` and are
applied in lexical order. See ``tools/patches/README.md`` for what each does.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
VENDOR = TOOLS / "vendor" / "make_rst.py"
PATCHES_DIR = TOOLS / "patches"
OUTPUT = TOOLS / "make_rst.py"


def _patches() -> list[Path]:
    return sorted(p for p in PATCHES_DIR.glob("*.patch") if p.is_file())


def _apply_patches(target: Path) -> None:
    """Apply every patch in ``patches/`` to ``target`` in-place."""
    patches = _patches()
    if not patches:
        sys.exit(f"no patches found in {PATCHES_DIR}")

    work_dir = target.parent
    for patch in patches:
        result = subprocess.run(
            ["patch", "-p1", "-i", str(patch), "--quiet", target.name],
            cwd=work_dir,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            sys.exit(f"failed to apply {patch.name}")
        print(f"  applied {patch.name}")


def build(output: Path) -> None:
    if not VENDOR.is_file():
        sys.exit(f"missing pristine vendor copy: {VENDOR}")
    print(f"vendor:  {VENDOR.relative_to(TOOLS)}")
    print(f"output:  {output.relative_to(TOOLS) if output.is_relative_to(TOOLS) else output}")
    with tempfile.TemporaryDirectory() as td:
        scratch = Path(td) / "make_rst.py"
        shutil.copy(VENDOR, scratch)
        _apply_patches(scratch)
        shutil.copy(scratch, output)
    print(f"wrote {output}")


def check() -> int:
    with tempfile.TemporaryDirectory() as td:
        scratch = Path(td) / "make_rst.py"
        shutil.copy(VENDOR, scratch)
        _apply_patches(scratch)
        if not OUTPUT.is_file():
            print(f"ERROR: {OUTPUT} does not exist — run build_make_rst.py", file=sys.stderr)
            return 1
        if not filecmp.cmp(scratch, OUTPUT, shallow=False):
            print(
                f"ERROR: {OUTPUT} is out of date with vendor+patches.\n"
                f"       Re-run: python {Path(__file__).name}",
                file=sys.stderr,
            )
            return 1
    print(f"{OUTPUT.name} is up-to-date with vendor+patches.")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify make_rst.py matches vendor+patches; exit non-zero otherwise",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=OUTPUT,
        help=f"output path (default: {OUTPUT})",
    )
    args = parser.parse_args()
    if args.check:
        sys.exit(check())
    build(args.output)


if __name__ == "__main__":
    main()
