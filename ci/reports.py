#!/usr/bin/env python3
"""Validate a JUnit report and reject incomplete or malformed results."""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

from common import ROOT, fail, main

SCRIPT_PATH = re.compile(r"res://[^\s:\"]+\.gd")


@dataclass
class Case:
    suite: str
    name: str
    kind: str
    detail: str


@dataclass
class Result:
    path: Path
    cases: int = 0
    rows: int = 0
    suites: int = 0
    failures: int = 0
    errors: int = 0
    skipped: int = 0
    bad: list[Case] = field(default_factory=list)
    red_names: set[str] = field(default_factory=set)


def _leaf_suites(root: ET.Element) -> list[ET.Element]:
    """Every suite that owns cases directly."""
    return [suite for suite in root.iter("testsuite") if suite.find("testsuite") is None]


def read(path: Path) -> Result:
    if not path.is_file():
        raise fail("no JUnit report at %s" % path)
    if path.stat().st_size == 0:
        raise fail("%s is empty, so the runner died before it wrote results" % path)
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as error:
        raise fail("%s is malformed XML (%s), so the run ended mid-write" % (path, error))

    result = Result(path=path)
    suites = _leaf_suites(root)
    result.suites = len(suites)
    for suite in suites:
        attrib = suite.attrib
        rows = suite.findall("testcase")
        if "doctest_version" in attrib:
            result.cases += len(rows)
        else:
            result.cases += int(attrib.get("tests", 0))
        result.failures += int(attrib.get("failures", 0))
        result.errors += int(attrib.get("errors", 0))
        result.skipped += int(attrib.get("skipped", 0))
        for case in rows:
            result.rows += 1
            name = case.get("name", "?")
            for kind in ("failure", "error"):
                for node in case.findall(kind):
                    text = (node.text or "").strip()
                    message = (node.get("message") or "").strip()
                    result.red_names.add(name)
                    result.bad.append(
                        Case(
                            suite=case.get("classname", suite.get("name", "?")),
                            name=name,
                            kind=kind,
                            detail="\n".join(p for p in (message, text) if p),
                        )
                    )
            for node in case.findall("skipped"):
                del node
                result.red_names.add(name)

    if not suites:
        attrib = root.attrib
        result.cases = int(attrib.get("tests", 0))
        result.failures = int(attrib.get("failures", 0))
        result.errors = int(attrib.get("errors", 0))
        result.skipped = int(attrib.get("skipped", 0))
    return result


def stale_against(path: Path, references: list[Path]) -> str | None:
    """A refusal when the report predates what it claims to describe."""
    newest = None
    for reference in references:
        if reference.exists() and (newest is None or reference.stat().st_mtime > newest.stat().st_mtime):
            newest = reference
    if newest is None:
        return None
    if path.stat().st_mtime >= newest.stat().st_mtime:
        return None
    return (
        "%s predates %s, so it describes an earlier build. Re-run the suite, "
        "or pass --allow-stale when a clock moved backwards." % (path, newest)
    )


def library_references() -> list[Path]:
    bin_dir = ROOT / "addons" / "networked" / "bin"
    return [Path(found) for found in glob.glob(str(bin_dir / "libnetworked.*")) if os.path.isfile(found)]


def read_floor(path: Path) -> int:
    floor = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            floor = int(line)
    return floor


def read_baseline(path: Path) -> dict[str, str]:
    known: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 3:
            known[parts[-1]] = parts[0]
    return known


def parse_errors(log: Path) -> list[tuple[str, str]]:
    """Every parse error in a console capture, with the script it names."""
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    found = []
    for index, line in enumerate(lines):
        if "Parse Error" not in line:
            continue
        where = ""
        for follow in lines[index : index + 3]:
            match = SCRIPT_PATH.search(follow)
            if match:
                where = match.group(0)
                break
        found.append((line.strip(), where))
    return found


def grade(
    path: Path,
    *,
    quiet: bool = False,
    expect_cases: int | None = None,
    expect_suites: int | None = None,
    allow_empty: bool = False,
    allow_stale: bool = False,
    baseline: Path | None = None,
    log: Path | None = None,
    references: list[Path] | None = None,
    out=print,
) -> int:
    if not allow_stale:
        stale = stale_against(path, references if references is not None else library_references())
        if stale:
            out("STALE %s" % stale)
            return 1

    result = read(path)
    unparsed = parse_errors(log) if log and log.is_file() else []
    out(
        "SUMMARY cases=%d rows=%d suites=%d fail=%d err=%d skip=%d parse=%d  (%s)"
        % (
            result.cases,
            result.rows,
            result.suites,
            result.failures,
            result.errors,
            result.skipped,
            len(unparsed),
            path,
        )
    )

    bad = result.bad
    stale_rows: list[str] = []
    if baseline is not None:
        known = read_baseline(baseline)
        expected = [case for case in bad if case.name in known]
        bad = [case for case in bad if case.name not in known]
        stale_rows = sorted(name for name in known if name not in result.red_names)
        out("BASELINE %s expected=%d new=%d stale=%d" % (baseline, len(expected), len(bad), len(stale_rows)))
        for name in stale_rows:
            out("STALE ROW %s passed, and the baseline holds it as %s" % (name, known[name]))

    if not quiet:
        for case in bad:
            out("\n%s %s.%s\n%s" % (case.kind.upper(), case.suite, case.name, case.detail))
        for script in sorted({script for _, script in unparsed if script}):
            out("\nPARSE ERROR %s -- this suite did not run" % script)

    refused = bool(bad) or bool(unparsed)
    if baseline is None and (result.failures or result.errors):
        refused = True
    if result.rows and result.rows < result.cases:
        out("TRUNCATED %d of %d cases wrote a row, so the run ended early" % (result.rows, result.cases))
        refused = True
    if result.cases == 0 and not allow_empty:
        out("EMPTY no case ran, and a filter that matches nothing is not a pass")
        refused = True
    if expect_cases is not None and result.cases < expect_cases:
        out("SHORT %d cases ran, %d recorded for this revision" % (result.cases, expect_cases))
        refused = True
    if expect_suites is not None and result.suites < expect_suites:
        out("THIN %d suites ran, %d recorded for this revision" % (result.suites, expect_suites))
        refused = True
    return 1 if refused else 0


FIXTURES = [
    (
        "a failing case",
        '<testsuites><testsuite name="n" tests="1" failures="1" errors="0">'
        '<testcase name="c"><failure>x: false</failure></testcase>'
        "</testsuite></testsuites>",
        {},
        1,
    ),
    (
        "an errored case",
        '<testsuites><testsuite name="n" tests="1" failures="0" errors="1">'
        '<testcase name="c"><error>SIGSEGV</error></testcase>'
        "</testsuite></testsuites>",
        {},
        1,
    ),
    (
        "a truncated run",
        '<testsuites><testsuite name="n" tests="5" failures="0" errors="0">'
        '<testcase name="c"></testcase></testsuite></testsuites>',
        {},
        1,
    ),
    (
        "an empty run",
        '<testsuites><testsuite name="n" tests="0" failures="0" errors="0">' "</testsuite></testsuites>",
        {},
        1,
    ),
    (
        "an aggregate the cases do not carry",
        '<testsuites><testsuite name="n" tests="2" failures="1" errors="0">'
        '<testcase name="a"></testcase><testcase name="b"></testcase>'
        "</testsuite></testsuites>",
        {},
        1,
    ),
    (
        "a shrunken corpus",
        '<testsuites><testsuite name="n" tests="2" failures="0" errors="0">'
        '<testcase name="a"></testcase><testcase name="b"></testcase>'
        "</testsuite></testsuites>",
        {"expect_cases": 3},
        1,
    ),
    (
        "a thinned suite count",
        '<testsuites><testsuite name="n" tests="1" failures="0" errors="0">'
        '<testcase name="a"></testcase></testsuite></testsuites>',
        {"expect_suites": 2},
        1,
    ),
    (
        "malformed XML",
        '<testsuites><testsuite name="n" tests="1"><testcase name="a">',
        {},
        1,
    ),
    (
        "an empty file",
        "",
        {},
        1,
    ),
    (
        "doctest counting assertions as tests",
        '<testsuites><testsuite name="all tests" errors="0" failures="0" '
        'tests="7" doctest_version="2.4.12"><testcase name="a"></testcase>'
        '<testcase name="b"></testcase></testsuite></testsuites>',
        {},
        0,
    ),
    (
        "nested suites counted once",
        '<testsuites><testsuite name="outer" tests="9" failures="0" errors="0">'
        '<testsuite name="inner" tests="2" failures="0" errors="0">'
        '<testcase name="a"></testcase><testcase name="b"></testcase>'
        "</testsuite></testsuite></testsuites>",
        {"expect_cases": 2},
        0,
    ),
    (
        "a clean run",
        '<testsuites><testsuite name="n" tests="2" failures="0" errors="0">'
        '<testcase name="a"></testcase><testcase name="b"></testcase>'
        "</testsuite></testsuites>",
        {},
        0,
    ),
]


def self_test(tmp: Path) -> int:
    swallowed: list[str] = []
    wrong = 0
    for name, xml, options, expected in FIXTURES:
        path = tmp / "results.xml"
        path.write_text(xml, encoding="utf-8")
        try:
            got = grade(
                path,
                quiet=True,
                allow_stale=True,
                out=swallowed.append,
                **options,
            )
        except Exception as error:
            del error
            got = 1
        ok = got == expected
        wrong += 0 if ok else 1
        print("%s %-38s exit=%d expected=%d" % ("ok  " if ok else "FAIL", name, got, expected))

    library = tmp / "libnetworked.linux.template_debug.x86_64.so"
    report = tmp / "old.xml"
    report.write_text(FIXTURES[-1][1], encoding="utf-8")
    library.write_bytes(b"newer")
    os.utime(report, (1, 1))
    got = grade(report, quiet=True, references=[library], out=swallowed.append)
    ok = got == 1
    wrong += 0 if ok else 1
    print("%s %-38s exit=%d expected=1" % ("ok  " if ok else "FAIL", "a stale report", got))

    log = tmp / "run.log"
    log.write_text(
        "Run Test Suite: x\n  Parse Error: bad token\n  res://tests/unit/x.gd:3\n",
        encoding="utf-8",
    )
    clean = tmp / "clean.xml"
    clean.write_text(FIXTURES[-1][1], encoding="utf-8")
    got = grade(clean, quiet=True, allow_stale=True, log=log, out=swallowed.append)
    ok = got == 1
    wrong += 0 if ok else 1
    print("%s %-38s exit=%d expected=1" % ("ok  " if ok else "FAIL", "a suite that did not parse", got))

    print("SELFTEST %d fixtures, %d wrong" % (len(FIXTURES) + 2, wrong))
    return 1 if wrong else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", nargs="?", type=Path)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--allow-empty", action="store_true")
    parser.add_argument("--allow-stale", action="store_true")
    parser.add_argument("--expect-cases", type=int)
    parser.add_argument("--expect-suites", type=int)
    parser.add_argument("--floor", type=Path, help="A file whose last number is the case floor.")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.self_test:
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            return self_test(Path(tmp))
    if args.report is None:
        raise fail("name the JUnit report to grade")
    expect_cases = args.expect_cases
    if args.floor is not None:
        floor = read_floor(args.floor)
        expect_cases = floor if expect_cases is None else max(expect_cases, floor)
    report = args.report
    if report.is_dir():
        report = report / "results.xml"
    return grade(
        report,
        quiet=args.quiet,
        expect_cases=expect_cases,
        expect_suites=args.expect_suites,
        allow_empty=args.allow_empty,
        allow_stale=args.allow_stale,
        baseline=args.baseline,
        log=args.log,
    )


if __name__ == "__main__":
    sys.exit(main(lambda: run_cli()))
