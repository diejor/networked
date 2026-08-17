#!/usr/bin/env python
"""Hold every ClassDB registration to a written disposition.

The campaign's end state keeps only the product surface registered
(`netw-ring0-elimination-plan-of-record.md` §2.9), and its exit condition
diffs the registered set against that STAY set exactly (§6). Until this
tool existed the STAY set was prose, so the one §6 term nothing could read
was the ClassDB census, and registrations grew without anyone deciding
their retirement.

`classdb_census.txt` is the disposition ledger: one row per registered
class, `<Class> stay` or `<Class> retire:<unit>`, where the unit is the
schedule row (`ZU-n`) whose landing deletes the registration. This tool
diffs `register_types.cpp` against it.

    check_classdb_census.py              # the census, grouped by disposition
    check_classdb_census.py --check      # exit 1 on an unlisted registration
                                         # or a classless retire row
    check_classdb_census.py --strict     # exit 1 unless registered == stay
                                         # exactly (the ZU-39/Z7 gate)
    check_classdb_census.py --self-test  # proves itself red, then green

A registration ABSENT from the ledger is a refusal, not a default: a class
registers with a disposition or it does not register. A ledger row whose
class is no longer registered is reported STALE without failing, so the
slice that unregisters deletes the row and a forgotten row stays visible.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGISTER = ROOT / "register_types.cpp"
CENSUS = Path(__file__).resolve().parent / "classdb_census.txt"
MACRO = re.compile(
    r"^\s*(GDREGISTER_(?:ABSTRACT_|INTERNAL_|RUNTIME_|VIRTUAL_)?CLASS)\s*"
    r"\(\s*([A-Za-z0-9_:]+)\s*\)"
)
ROW = re.compile(r"^([A-Za-z0-9_]+)\s+(stay|retire:ZU-\d+)\s*$")


def registered(text):
    """Every registered class name, unqualified, with its macro."""
    rows = {}
    for line in text.splitlines():
        hit = MACRO.match(line)
        if hit:
            rows[hit.group(2).split("::")[-1]] = hit.group(1)
    return rows


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


def census(reg, rows):
    stay = sorted(n for n in reg if rows.get(n) == "stay")
    retire = {}
    for name in reg:
        verdict = rows.get(name, "")
        if verdict.startswith("retire:"):
            retire.setdefault(verdict.split(":", 1)[1], []).append(name)
    unlisted = sorted(n for n in reg if n not in rows)
    stale = sorted(n for n in rows if n not in reg)
    return stay, retire, unlisted, stale


def report(reg, rows, bad):
    stay, retire, unlisted, stale = census(reg, rows)
    retiring = sum(len(v) for v in retire.values())
    print(
        "CLASSDB registered=%d stay=%d retiring=%d unlisted=%d stale=%d"
        % (len(reg), len(stay), retiring, len(unlisted), len(stale))
    )
    for unit in sorted(retire):
        print("  retire at %s: %d" % (unit, len(retire[unit])))
        for name in sorted(retire[unit]):
            print("    %s" % name)
    for name in unlisted:
        print("  UNLISTED %s (%s)" % (name, reg[name]))
    for name in stale:
        print("  STALE %s: row without a registration" % name)
    for at, line in bad:
        print("  UNPARSED %s:%d: %s" % (CENSUS.name, at, line))
    return stay, retire, unlisted, stale


def run_check(reg, rows, bad):
    _, _, unlisted, _ = census(reg, rows)
    refused = len(unlisted) + len(bad)
    for name in unlisted:
        print(
            "REFUSED %s is registered with no disposition: add a"
            " `stay` or `retire:ZU-n` row to %s" % (name, CENSUS.name)
        )
    for at, line in bad:
        print("REFUSED %s:%d is not a disposition row: %s" % (CENSUS.name, at, line))
    return 1 if refused else 0


def run_strict(reg, rows, bad):
    stay, retire, unlisted, _ = census(reg, rows)
    refused = len(unlisted) + len(bad)
    for unit in sorted(retire):
        for name in sorted(retire[unit]):
            print("REFUSED %s is still registered and owed to %s" % (name, unit))
            refused += 1
    for name in unlisted:
        print("REFUSED %s is registered with no disposition" % name)
    return 1 if refused else 0


def self_test():
    reg = registered(
        "void f() {\n"
        "\tGDREGISTER_CLASS(netw::NetwKept);\n"
        "\tGDREGISTER_ABSTRACT_CLASS(netw::NetwGoing);\n"
        "\tGDREGISTER_CLASS(netw_test::Stray);\n"
        "}\n"
    )
    rows, bad = ledger("NetwKept stay\nNetwGoing retire:ZU-39\nNetwGone stay\nnot a row\n")
    stay, retire, unlisted, stale = census(reg, rows)
    checks = [
        ("three registrations parse", len(reg) == 3),
        ("the abstract macro is kept apart", reg["NetwGoing"].startswith("GDREGISTER_ABSTRACT")),
        ("stay resolves", stay == ["NetwKept"]),
        ("retire groups by unit", retire == {"ZU-39": ["NetwGoing"]}),
        ("an unlisted registration refuses", unlisted == ["Stray"]),
        ("a stale row reports without a registration", stale == ["NetwGone"]),
        ("a malformed row is refused", len(bad) == 1),
        ("--check refuses the stray and the bad row", run_check(reg, rows, bad) == 1),
        ("--strict refuses the retiring row too", run_strict(reg, rows, bad) == 1),
        (
            "--strict passes a pure stay tree",
            run_strict({"NetwKept": "GDREGISTER_CLASS"}, {"NetwKept": "stay"}, []) == 0,
        ),
    ]
    failed = [name for name, ok in checks if not ok]
    for name, ok in checks:
        print("%s %s" % ("ok " if ok else "RED", name))
    return 1 if failed else 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    reg = registered(REGISTER.read_text())
    rows, bad = ledger(CENSUS.read_text()) if CENSUS.exists() else ({}, [])
    if "--check" in sys.argv:
        return run_check(reg, rows, bad)
    if "--strict" in sys.argv:
        return run_strict(reg, rows, bad)
    report(reg, rows, bad)
    return 0


if __name__ == "__main__":
    sys.exit(main())
