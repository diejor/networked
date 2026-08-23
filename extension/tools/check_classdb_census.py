#!/usr/bin/env python
"""Hold every ClassDB registration to a written disposition.

The campaign's end state keeps only the product surface registered
(`netw-ring0-elimination-plan-of-record.md` §2.9), and its exit condition
diffs the registered set against that STAY set exactly (§6). Until this
tool existed the STAY set was prose, so the one §6 term nothing could read
was the ClassDB census, and registrations grew without anyone deciding
their retirement.

`classdb_census.txt` is the disposition ledger: one row per registered
class, `<Class> stay`, `<Class> stay <argument>`, `<Class> retire:<unit>`,
or `<Class> internal <reason>`. `stay` is the published surface, and its
optional argument names what holds it there, which is required of a class
the permanent keep set reaches (ZD-34). `retire:<unit>` names the track
(`ZT-n`) whose crossing deletes the registration. `internal`
(ZD-26) is `GDREGISTER_INTERNAL_CLASS`: the class stays registered for a
C++ core to construct but is never named from GDScript, so it carries no
XML and is off the books for `--strict`. This tool diffs
`register_types.cpp` against the ledger.

    check_classdb_census.py              # the census, grouped by disposition
    check_classdb_census.py --check      # exit 1 on an unlisted registration
                                         # or a classless retire row
    check_classdb_census.py --strict     # exit 1 unless every EXPOSED
                                         # registration (registered minus
                                         # internal) is in the stay set
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
ROW = re.compile(
    r"^([A-Za-z0-9_]+)\s+(stay(?:\s+\S.*)?|retire:ZT-\d+|internal\s+\S.*)\s*$"
)


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


def is_stay(verdict):
    return verdict == "stay" or verdict.startswith("stay ")


def census(reg, rows):
    stay = sorted(n for n in reg if is_stay(rows.get(n, "")))
    retire = {}
    internal = []
    for name in reg:
        verdict = rows.get(name, "")
        if verdict.startswith("retire:"):
            retire.setdefault(verdict.split(":", 1)[1], []).append(name)
        elif verdict.startswith("internal"):
            internal.append(name)
    internal.sort()
    unlisted = sorted(n for n in reg if n not in rows)
    stale = sorted(n for n in rows if n not in reg)
    return stay, retire, internal, unlisted, stale


def report(reg, rows, bad):
    stay, retire, internal, unlisted, stale = census(reg, rows)
    retiring = sum(len(v) for v in retire.values())
    print(
        "CLASSDB registered=%d stay=%d retiring=%d internal=%d unlisted=%d"
        " stale=%d"
        % (len(reg), len(stay), retiring, len(internal), len(unlisted), len(stale))
    )
    for unit in sorted(retire):
        print("  retire at %s: %d" % (unit, len(retire[unit])))
        for name in sorted(retire[unit]):
            print("    %s" % name)
    if internal:
        print("  internal: %d" % len(internal))
        for name in internal:
            print("    %s (%s)" % (name, rows[name]))
    for name in unlisted:
        print("  UNLISTED %s (%s)" % (name, reg[name]))
    for name in stale:
        print("  STALE %s: row without a registration" % name)
    for at, line in bad:
        print("  UNPARSED %s:%d: %s" % (CENSUS.name, at, line))
    return stay, retire, internal, unlisted, stale


def run_check(reg, rows, bad):
    _, _, _, unlisted, _ = census(reg, rows)
    refused = len(unlisted) + len(bad)
    for name in unlisted:
        print(
            "REFUSED %s is registered with no disposition: add a"
            " `stay`, `retire:ZT-n` or `internal <reason>` row to %s"
            % (name, CENSUS.name)
        )
    for at, line in bad:
        print("REFUSED %s:%d is not a disposition row: %s" % (CENSUS.name, at, line))
    return 1 if refused else 0


def run_strict(reg, rows, bad):
    stay, retire, internal, unlisted, _ = census(reg, rows)
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
        "\tGDREGISTER_INTERNAL_CLASS(netw::NetwHidden);\n"
        "\tGDREGISTER_CLASS(netw_test::Stray);\n"
        "}\n"
    )
    rows, bad = ledger(
        "NetwKept stay held by context/networked.gd, which never dies\n"
        "NetwGoing retire:ZT-6\n"
        "NetwHidden internal held by NetwSessionCore, never named\n"
        "NetwGone stay\n"
        "not a row\n"
    )
    stay, retire, internal, unlisted, stale = census(reg, rows)
    checks = [
        ("four registrations parse", len(reg) == 4),
        ("the abstract macro is kept apart", reg["NetwGoing"].startswith("GDREGISTER_ABSTRACT")),
        ("the internal macro is kept apart", reg["NetwHidden"] == "GDREGISTER_INTERNAL_CLASS"),
        ("stay resolves through its argument", stay == ["NetwKept"]),
        ("retire groups by unit", retire == {"ZT-6": ["NetwGoing"]}),
        (
            "internal resolves and carries a reason",
            internal == ["NetwHidden"] and rows["NetwHidden"].startswith("internal "),
        ),
        ("an unlisted registration refuses", unlisted == ["Stray"]),
        ("a stale row reports without a registration", stale == ["NetwGone"]),
        ("a malformed row is refused", len(bad) == 1),
        ("--check refuses the stray and the bad row", run_check(reg, rows, bad) == 1),
        ("--strict refuses the retiring row too", run_strict(reg, rows, bad) == 1),
        (
            "--strict passes a pure stay tree",
            run_strict({"NetwKept": "GDREGISTER_CLASS"}, {"NetwKept": "stay"}, []) == 0,
        ),
        (
            "--strict takes an internal row off the books",
            run_strict(
                {
                    "NetwKept": "GDREGISTER_CLASS",
                    "NetwHidden": "GDREGISTER_INTERNAL_CLASS",
                },
                {
                    "NetwKept": "stay",
                    "NetwHidden": "internal held by a core",
                },
                [],
            )
            == 0,
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
