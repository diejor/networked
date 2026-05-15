#!/usr/bin/env python3
"""Rebuild ``tools/patches/*.patch`` from ``vendor/make_rst.py`` vs ``make_rst.py``.

Use this when you've edited ``tools/make_rst.py`` directly and want to push
those changes back into the themed patch files. Hunks are bucketed by their
original-file (vendor) start line — keep the bucket boundaries in sync with
the patch index in ``patches/README.md`` if you reorganise.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
VENDOR = TOOLS / "vendor" / "make_rst.py"
WORKING = TOOLS / "make_rst.py"
PATCHES_DIR = TOOLS / "patches"

BUCKETS: list[tuple[str, str, int, int]] = [
    (
        "0001-standalone-addon-pathing-and-defaults.patch",
        "Run against an addon-only XML set: addon-relative sys.path, api_type-driven editor detection, engine-class group fallback, godot-xref helper, PRIMITIVE_TYPES, friendlier warning summary.",
        0,
        900,
    ),
    (
        "0002-external-godot-refs-and-abstract.patch",
        "Emit :godot: roles for symbols outside the local XML set; add |abstract|, missing-base grouped-index guard, nested-class link targets.",
        900,
        1860,
    ),
    (
        "0003-debug-tag-stack-and-engine-class-tags.patch",
        "Track BBCode tag depth, warn on illegal nesting, recognise unknown uppercase tags as engine classes.",
        1860,
        2120,
    ),
    (
        "0004-ref-resolution-fallback-and-warnings.patch",
        "Downgrade unresolved [method/member/signal/...] refs to warnings and emit external :godot: links instead of broken :ref: anchors; @GlobalScope guard.",
        2120,
        2295,
    ),
    (
        "0005-tag-handling-and-codeblock-escape.patch",
        "Handle [color]/[font] tags, guard empty [br] tails, fall back uppercase invalid tags to make_type(), fix codeblock paragraph-eating escape.",
        2295,
        10**9,
    ),
]

HUNK_RE = re.compile(r"^@@ -(\d+)")


def main() -> None:
    if not VENDOR.is_file():
        sys.exit(f"missing {VENDOR}")
    if not WORKING.is_file():
        sys.exit(f"missing {WORKING}")

    result = subprocess.run(
        ["diff", "-u", str(VENDOR), str(WORKING)],
        capture_output=True,
    )
    # diff exits 1 when files differ — that's what we expect.
    if result.returncode not in (0, 1):
        sys.stderr.write(result.stderr.decode("utf-8", errors="replace"))
        sys.exit("diff failed")
    if result.returncode == 0:
        sys.exit("vendor and working file are identical — nothing to do.")

    stdout = result.stdout.decode("utf-8")
    lines = stdout.splitlines(keepends=True)
    body = lines[2:]  # drop --- / +++ from real diff
    hunks: list[list[str]] = []
    cur: list[str] = []
    for line in body:
        if line.startswith("@@"):
            if cur:
                hunks.append(cur)
            cur = [line]
        else:
            if cur:
                cur.append(line)
    if cur:
        hunks.append(cur)

    PATCHES_DIR.mkdir(parents=True, exist_ok=True)
    # Remove any existing themed patches so renames/removals propagate.
    for old in PATCHES_DIR.glob("*.patch"):
        old.unlink()

    for fname, description, lo, hi in BUCKETS:
        chosen: list[list[str]] = []
        for hunk in hunks:
            m = HUNK_RE.match(hunk[0])
            if not m:
                continue
            start = int(m.group(1))
            if lo <= start < hi:
                chosen.append(hunk)
        if not chosen:
            print(f"  (skip) {fname} — no hunks in [{lo}, {hi})")
            continue
        p = PATCHES_DIR / fname
        with p.open("w", encoding="utf-8", newline="\n") as f:
            f.write(f"# {description}\n")
            f.write("--- a/make_rst.py\n")
            f.write("+++ b/make_rst.py\n")
            for hunk in chosen:
                f.writelines(hunk)
        print(f"  wrote {p.relative_to(TOOLS)}  ({len(chosen)} hunks)")


if __name__ == "__main__":
    main()
