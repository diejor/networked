#!/usr/bin/env python
"""Keep engine diagnostics behind Networked's dual-target vocabulary.

Two rules, and the second exists because the first one's own sweep broke it.

1. A raw engine guard or profiler seam bypasses the NETW_* macros.
2. A `sys::` subsystem constant inside a binding declaration is a published
   API name spelled by the log table. The two happen to agree today, so
   nothing observable is wrong, and that is exactly why only a check finds
   it: renaming a subsystem would silently rename a method, a parameter or a
   property on the public surface.
"""

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


BINDING_CALL = re.compile(r"\b(?:D_METHOD|PropertyInfo)\s*\(")
SUBSYSTEM_USE = re.compile(r"\bsys::[A-Z_]+\b")


def binding_subsystem_hits(text):
    """Every `sys::X` that sits inside a D_METHOD or PropertyInfo argument list.

    Scanned by matching parentheses rather than by line, because the tree's
    formatter puts each argument on its own line and a line-local regex cannot
    see which call it belongs to.
    """
    hits = []
    for call in BINDING_CALL.finditer(text):
        depth = 0
        at = call.end() - 1
        while at < len(text):
            if text[at] == "(":
                depth += 1
            elif text[at] == ")":
                depth -= 1
                if depth == 0:
                    break
            at += 1
        body = text[call.end():at]
        for use in SUBSYSTEM_USE.finditer(body):
            hits.append((text.count("\n", 0, call.end() + use.start()) + 1, use.group(0)))
    return hits


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
                hits.append((path, line_number, match.group(0), "raw"))
        for line_number, spelling in binding_subsystem_hits(
            path.read_text(errors="replace")
        ):
            hits.append((path, line_number, spelling, "binding"))

    for path, line_number, spelling, kind in hits:
        try:
            shown = path.relative_to(ROOT)
        except ValueError:
            shown = path
        out(
            "{}:{}: {}".format(
                shown,
                line_number,
                "raw diagnostic {} bypasses NETW_*".format(spelling)
                if kind == "raw"
                else "{} names a published binding; write the literal".format(
                    spelling
                ),
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
        path.write_text(
            "void bind() {\n"
            "    ClassDB::bind_method(\n"
            "        D_METHOD(\n"
            "            \"encode\",\n"
            "            sys::TICK,\n"
            "            \"ack\"\n"
            "        ),\n"
            "        &probe\n"
            "    );\n"
            "}\n"
        )
        if check((source,), set(), lambda _line: None) != 1:
            print("self-test failed to refuse a subsystem inside a binding")
            return 1
        path.write_text(
            "void log() { NETW_TRACE(sys::TICK, \"a tick passed\"); }\n"
        )
        if check((source,), set(), lambda _line: None) != 0:
            print("self-test refused a subsystem in the place it belongs")
            return 1
    print("self-test: the check refuses raw seams and misplaced subsystems")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return self_test() if args.self_test else check()


if __name__ == "__main__":
    raise SystemExit(main())
