#!/usr/bin/env python
"""Keep engine diagnostics behind Networked's dual-target vocabulary."""

import argparse
import re
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = (ROOT / "include" / "netw", ROOT / "src")
ALLOWED = {
    ROOT / "include" / "netw" / "profile.hpp",
    ROOT / "src" / "log.cpp",
    ROOT / "src" / "profile.cpp",
}
RAW_DIAGNOSTIC = re.compile(
    r"\b(?:ERR_FAIL\w*|WARN_PRINT\w*|ERR_PRINT\w*|CRASH_\w*|"
    r"DEV_ASSERT|ZoneScoped\w*|ZoneText\w*|ZoneName\w*|ZoneColor\w*|"
    r"ZoneValue\w*|ZoneNamed\w*|Tracy\w*|FrameMark\w*|"
    r"gd::push_(?:warning|error)(?:_at)?)\b"
)


def source_paths(roots):
    for root in roots:
        for suffix in ("*.hpp", "*.cpp"):
            yield from root.rglob(suffix)


def check(roots=SCAN_ROOTS, allowed=ALLOWED, out=print):
    hits = []
    for path in sorted(source_paths(roots)):
        if path in allowed:
            continue
        for line_number, line in enumerate(
            path.read_text(errors="replace").splitlines(),
            1,
        ):
            match = RAW_DIAGNOSTIC.search(line)
            if match:
                hits.append((path, line_number, match.group(0)))

    for path, line_number, spelling in hits:
        try:
            shown = path.relative_to(ROOT)
        except ValueError:
            shown = path
        out(
            "{}:{}: raw diagnostic {} bypasses NETW_*".format(
                shown,
                line_number,
                spelling,
            )
        )
    if hits:
        return 1
    out("production diagnostics use the NETW_* vocabulary")
    return 0


def self_test():
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        source = root / "src"
        source.mkdir()
        path = source / "probe.cpp"
        path.write_text("void probe() { ERR_FAIL_COND(true); }\n")
        if check((source,), set(), lambda _line: None) != 1:
            print("self-test failed to refuse a raw engine guard")
            return 1
        path.write_text("void probe() { NETW_ERR_COND(true, \"x\", \"y\"); }\n")
        if check((source,), set(), lambda _line: None) != 0:
            print("self-test refused the owned guard")
            return 1
    print("self-test: the check refuses raw diagnostic seams")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return self_test() if args.self_test else check()


if __name__ == "__main__":
    raise SystemExit(main())
