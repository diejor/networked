#!/usr/bin/env python3
"""Decide which verification lanes a change can reach."""

from __future__ import annotations

import argparse
import fnmatch
from pathlib import Path

from common import fail, github_output, main, run

LANES: dict[str, tuple[str, ...]] = {
    "native": (
        "extension/**",
        "ci/**",
        "addons/networked/**",
        ".github/**",
    ),
    "hosted": (
        "extension/**",
        "ci/**",
        "tests/native/**",
        ".github/**",
    ),
    "module": (
        "extension/**",
        "ci/engines.json",
        "ci/platforms.json",
        "ci/*.py",
        ".github/workflows/module.yml",
        ".github/workflows/ci.yml",
    ),
    "gdscript": (
        "addons/networked/**",
        "tests/**",
        "ci/**",
        "project.godot",
        ".github/**",
    ),
    "examples": (
        "examples/**",
        "addons/networked/**",
        "ci/**",
        "project.godot",
        ".github/**",
    ),
    "docs": (
        "docs/**",
        "extension/doc_classes/**",
        "addons/networked/**",
        "ci/**",
        ".readthedocs.yml",
        ".github/**",
    ),
}


def matches(path: str, pattern: str) -> bool:
    if pattern.endswith("/**"):
        return path == pattern[:-3] or path.startswith(pattern[:-2])
    return fnmatch.fnmatch(path, pattern)


def lanes_for(paths: list[str]) -> dict[str, bool]:
    return {
        lane: any(matches(path, pattern) for path in paths for pattern in patterns) for lane, patterns in LANES.items()
    }


def changed_paths(base: str, head: str) -> list[str]:
    completed = run(["git", "diff", "--name-only", "%s...%s" % (base, head)], capture=True, timeout=300)
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="", help="Empty means every lane runs.")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--paths-from", type=Path, help="Read the changed paths from a file instead of git.")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.paths_from is not None:
        paths = [line.strip() for line in args.paths_from.read_text(encoding="utf-8").splitlines() if line.strip()]
        lanes = lanes_for(paths)
    elif not args.base:
        print("no base revision, so every lane runs")
        lanes = {lane: True for lane in LANES}
    else:
        paths = changed_paths(args.base, args.head)
        if not paths:
            raise fail("the diff between %s and %s is empty" % (args.base, args.head))
        lanes = lanes_for(paths)
        print("%d changed path(s)" % len(paths))

    for lane in sorted(lanes):
        print("%-10s %s" % (lane, "run" if lanes[lane] else "skip"))
    github_output(**{lane: str(value).lower() for lane, value in lanes.items()})
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
