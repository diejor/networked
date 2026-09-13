#!/usr/bin/env python
"""Hold every GDScript test suite to a written disposition."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests"
CENSUS = TESTS / "suite_census.txt"
EXTENDS = re.compile(r"^extends (NetwTestSuite|GdUnitTestSuite)\s*$", re.M)
ROW = re.compile(r"^(\S+)\s+(boundary\s+\S.*|port\s+\S+)\s*$")


def suites(tests_dir):
    """Every GDScript suite path under tests_dir, repo-relative and sorted."""
    found = []
    for path in tests_dir.rglob("*.gd"):
        if EXTENDS.search(path.read_text(errors="ignore")):
            found.append(str(path.relative_to(tests_dir.parent)))
    return sorted(found)


def ledger(text):
    rows = {}
    bad = []
    for at, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        hit = ROW.match(line)
        if hit:
            rows[hit.group(1)] = hit.group(2)
        else:
            bad.append((at, line))
    return rows, bad


def census(found, rows):
    boundary = sorted(p for p in found if rows.get(p, "").startswith("boundary"))
    port = {}
    for path in found:
        verdict = rows.get(path, "")
        if verdict.startswith("port"):
            port.setdefault(verdict.split(None, 1)[1], []).append(path)
    unlisted = sorted(p for p in found if p not in rows)
    stale = sorted(p for p in rows if p not in found)
    return boundary, port, unlisted, stale


def report(found, rows, bad):
    boundary, port, unlisted, stale = census(found, rows)
    porting = sum(len(v) for v in port.values())
    print(
        "SUITES total=%d boundary=%d port=%d unlisted=%d stale=%d"
        % (len(found), len(boundary), porting, len(unlisted), len(stale))
    )
    for band in sorted(port):
        print("  port with %s: %d" % (band, len(port[band])))
    if boundary:
        print("  boundary: %d" % len(boundary))
        for path in boundary:
            print("    %s (%s)" % (path, rows[path].split(None, 1)[1]))
    for path in unlisted:
        print("  UNLISTED %s" % path)
    for path in stale:
        print("  STALE %s: row without a suite" % path)
    for at, line in bad:
        print("  UNPARSED %s:%d: %s" % (CENSUS.name, at, line))
    return boundary, port, unlisted, stale


def run_check(found, rows, bad):
    _, _, unlisted, _ = census(found, rows)
    for path in unlisted:
        print("REFUSED %s has no row in %s" % (path, CENSUS.name))
    for at, line in bad:
        print("REFUSED %s:%d is not a disposition row: %s" % (CENSUS.name, at, line))
    return len(unlisted) + len(bad)


def run_strict(found, rows):
    _, port, unlisted, _ = census(found, rows)
    for path in unlisted:
        print("REFUSED %s has no row" % path)
    for band in sorted(port):
        for path in sorted(port[band]):
            print("REFUSED %s is still GDScript and owed to %s" % (path, band))
    return len(unlisted) + sum(len(v) for v in port.values())


def self_test():
    """Prove the checker red on each refusal, then green on a clean pair."""
    found = ["tests/unit/a.gd", "tests/unit/b.gd"]
    clean = "tests/unit/a.gd boundary §9.4 the published test kit\n" "tests/unit/b.gd port session\n"

    rows, bad = ledger(clean)
    assert run_check(found, rows, bad) == 0, "clean pair must pass --check"
    assert run_strict(found, rows) == 1, "a port row must fail --strict"

    rows, bad = ledger("tests/unit/a.gd boundary §9.4 the published test kit\n")
    assert run_check(found, rows, bad) == 1, "a suite with no row must refuse"

    rows, bad = ledger(clean + "tests/unit/c.gd port session\n")
    assert run_check(found, rows, bad) == 0, "a stale row must not refuse"
    assert census(found, rows)[3] == ["tests/unit/c.gd"], "stale must report"

    rows, bad = ledger("tests/unit/a.gd maybe\ntests/unit/b.gd port session\n")
    assert run_check(found, rows, bad) == 2, "an unparsed row must refuse"

    rows, bad = ledger("tests/unit/a.gd boundary\ntests/unit/b.gd port session\n")
    assert run_check(found, rows, bad) == 2, "boundary needs an argument"

    only_boundary = (
        "tests/unit/a.gd boundary §9.4 the published test kit\n" "tests/unit/b.gd boundary §9.3 real sockets\n"
    )
    rows, _ = ledger(only_boundary)
    assert run_strict(found, rows) == 0, "an all-boundary census passes --strict"
    print("SELF-TEST ok")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    found = suites(TESTS)
    rows, bad = ledger(CENSUS.read_text())
    if "--check" in argv:
        return 1 if run_check(found, rows, bad) else 0
    if "--strict" in argv:
        return 1 if run_strict(found, rows) else 0
    report(found, rows, bad)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
