#!/usr/bin/env python
"""Fail if a `[Hosted]` case file is not one the module tier can reach.

A `[Hosted]` case claims to be tier-portable: written once, run in both. Each
tier finds it a different way. The library build globs every `.cpp` under
`tests/` from `sources.py`, so a case file needs no registration at all. The
engine compiles only the headers directly under `modules/networked/tests/`, so
it reaches the same case files ONLY through `test_networked_hosted.h`.

Leaving a file out of that header is therefore invisible. The library tier runs
the cases and reports them green, the module tier does not have them at all, and
a filter matching no cases still exits zero. It has happened: nine session cases
ran green in one tier and were absent from the other, and only a hand-run
`--list-test-cases` and a grep caught it.

Three directions are checked, because each is a different lie:

  a file with `[Hosted]` tags that the header does not include is a case
  claiming portability it does not have;

  a file the header includes that declares no `[Hosted]` tag is a file paying
  the module tier's compile for nothing, and usually means a tag was removed
  without the include following it;

  a `[Hosted]` case inside `#if defined(NETW_TIER_HOSTED)` is the same lie as
  the first, worn as a disguise the first two directions cannot see through.
  The include is present and the tag is present, so both of those pass, while
  the engine preprocesses the case away and runs nothing. That is how eleven
  files carrying forty-three `[Hosted]` cases read as portable in every count
  this tool produced.

    check_hosted_registry.py              # PASS or the offending files, exit 0/1
    check_hosted_registry.py --self-test  # proves itself red, then green
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
REGISTRY = TESTS / "test_networked_hosted.h"

# A real case tag, not the word in a comment. Every case file that says it is
# NOT `[Hosted]` says so in prose, and prose must not count as a declaration.
TAG = re.compile(r'"\[Networked\]\[[A-Za-z0-9]+\]\[Hosted\]')
INCLUDE = re.compile(r'^\s*#include\s+"([A-Za-z0-9_]+\.cpp)"', re.MULTILINE)

OPENS = re.compile(r"^\s*#\s*if(n?def)?\b(.*)$")
ELIF = re.compile(r"^\s*#\s*elif\b(.*)$")
ELSE = re.compile(r"^\s*#\s*else\b")
ENDIF = re.compile(r"^\s*#\s*endif\b")

# What a condition means to the ENGINE's preprocessor, or None where this tool
# has no opinion. An unreadable condition must read as "the module tier
# compiles it", so an unfamiliar guard is never mistaken for an accusation.
CONDITIONS = (
    (re.compile(r"^\s*defined\s*\(?\s*NETW_TIER_HOSTED\s*\)?\s*$"), False),
    (re.compile(r"^\s*!\s*defined\s*\(?\s*NETW_TIER_HOSTED\s*\)?\s*$"), True),
    (re.compile(r"^\s*defined\s*\(?\s*NETW_TIER_MODULE\s*\)?\s*$"), True),
    (re.compile(r"^\s*!\s*defined\s*\(?\s*NETW_TIER_MODULE\s*\)?\s*$"), False),
)


def module_verdict(condition):
    for pattern, verdict in CONDITIONS:
        if pattern.match(condition):
            return verdict
    return None


def preprocessed_away(text):
    """The `[Hosted]` tags in `text` the engine's preprocessor deletes.

    Returned as line numbers, so a report names where to look. A conditional
    this tool cannot read leaves its region compiled, because the cost of a
    false accusation is a check people learn to talk past.
    """
    away = []
    frames = []
    for number, line in enumerate(text.split("\n"), start=1):
        opening = OPENS.match(line)
        if opening:
            if opening.group(1):
                frames.append(None)
            else:
                frames.append(module_verdict(opening.group(2)))
        elif ELIF.match(line):
            if frames:
                frames[-1] = None
        elif ELSE.match(line):
            if frames:
                held = frames[-1]
                frames[-1] = None if held is None else not held
        elif ENDIF.match(line):
            if frames:
                frames.pop()
        if TAG.search(line) and any(frame is False for frame in frames):
            away.append(number)
    return away


def tagged_files(tests_dir):
    """Every `.cpp` under the tests directory that declares a `[Hosted]` case."""
    found = set()
    for path in sorted(tests_dir.rglob("*.cpp")):
        if TAG.search(path.read_text(errors="replace")):
            found.add(path.name)
    return found


def guarded_cases(tests_dir):
    """Each file's `[Hosted]` tags that no module build ever compiles."""
    found = {}
    for path in sorted(tests_dir.rglob("*.cpp")):
        away = preprocessed_away(path.read_text(errors="replace"))
        if away:
            found[path.name] = away
    return found


def registered_files(registry):
    if not registry.is_file():
        return set()
    return set(INCLUDE.findall(registry.read_text(errors="replace")))


def check(tests_dir, registry, out=print):
    tagged = tagged_files(tests_dir)
    registered = registered_files(registry)

    unreachable = sorted(tagged - registered)
    for name in unreachable:
        out(
            "declares a [Hosted] case the module tier cannot reach: {} "
            "(add it to {})".format(name, registry.name)
        )

    pointless = sorted(registered - tagged)
    for name in pointless:
        out(
            "{} includes {}, which declares no [Hosted] case".format(
                registry.name, name
            )
        )

    guarded = guarded_cases(tests_dir)
    for name in sorted(guarded):
        lines = ", ".join(str(number) for number in guarded[name][:6])
        more = "" if len(guarded[name]) <= 6 else ", ..."
        out(
            "{} tags {} [Hosted] case(s) the module tier preprocesses away, at "
            "line(s) {}{} (drop the tier guard, or drop the tag)".format(
                name, len(guarded[name]), lines, more
            )
        )

    if unreachable or pointless or guarded:
        return 1
    out("{} [Hosted] case file(s), all reachable from both tiers".format(len(tagged)))
    return 0


HOSTED_CASE = 'TEST_CASE("[Networked][Probe][Hosted] one") {}\n'
TIER_A_ONLY = "// Deliberately not [Hosted]: it reads res://.\n"
GUARDED_CASE = (
    "#if defined(NETW_TIER_HOSTED)\n" + HOSTED_CASE + "#endif\n"
)
UNREADABLE_GUARD = "#if defined(SOMETHING_ELSE)\n" + HOSTED_CASE + "#endif\n"
GUARD_WITH_ELSE = (
    "#if defined(NETW_TIER_MODULE)\n"
    "#else\n" + HOSTED_CASE + "#endif\n"
)


def self_test():
    """Proves the check red before it is trusted green."""
    import tempfile

    failed = 0
    swallowed = []

    def hush(text):
        swallowed.append(text)

    with tempfile.TemporaryDirectory() as work:
        tests = Path(work) / "tests"
        tests.mkdir()
        registry = tests / "test_networked_hosted.h"
        (tests / "probe_tests.cpp").write_text(HOSTED_CASE)
        # A tier-2a-only file naming [Hosted] in prose must NOT be demanded.
        (tests / "rig_tests.cpp").write_text(TIER_A_ONLY)

        extra = tests / "extra_tests.cpp"
        cases = [
            ("an unregistered [Hosted] file", None, '#include "other_tests.cpp"\n', 1),
            ("a registered file with no tag", None, '#include "rig_tests.cpp"\n', 1),
            ("a correct registry", None, '#include "probe_tests.cpp"\n', 0),
            (
                "a [Hosted] case behind the tier guard",
                GUARDED_CASE,
                '#include "probe_tests.cpp"\n#include "extra_tests.cpp"\n',
                1,
            ),
            (
                "a [Hosted] case behind an #else the tier takes",
                GUARD_WITH_ELSE,
                '#include "probe_tests.cpp"\n#include "extra_tests.cpp"\n',
                1,
            ),
            (
                "a [Hosted] case behind a guard this tool cannot read",
                UNREADABLE_GUARD,
                '#include "probe_tests.cpp"\n#include "extra_tests.cpp"\n',
                0,
            ),
        ]
        for name, body, registry_body, expected in cases:
            if body is None:
                extra.unlink(missing_ok=True)
            else:
                extra.write_text(body)
            registry.write_text(registry_body)
            got = check(tests, registry, hush)
            status = "ok" if got == expected else "SELF-TEST FAILED"
            print("  {:44s} exit {} ({})".format(name, got, status))
            failed += 0 if got == expected else 1

    if failed:
        print("{} self-test case(s) did not behave as declared".format(failed))
    else:
        print("self-test: the check refuses what it claims to refuse")
    return 1 if failed else 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    return check(TESTS, REGISTRY)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
