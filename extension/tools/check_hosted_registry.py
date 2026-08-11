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

Both directions are checked, because each is a different lie:

  a file with `[Hosted]` tags that the header does not include is a case
  claiming portability it does not have;

  a file the header includes that declares no `[Hosted]` tag is a file paying
  the module tier's compile for nothing, and usually means a tag was removed
  without the include following it.

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


def tagged_files(tests_dir):
    """Every `.cpp` under the tests directory that declares a `[Hosted]` case."""
    found = set()
    for path in sorted(tests_dir.rglob("*.cpp")):
        if TAG.search(path.read_text(errors="replace")):
            found.add(path.name)
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

    if unreachable or pointless:
        return 1
    out("{} [Hosted] case file(s), all reachable from both tiers".format(len(tagged)))
    return 0


HOSTED_CASE = 'TEST_CASE("[Networked][Probe][Hosted] one") {}\n'
TIER_A_ONLY = "// Deliberately not [Hosted]: it reads res://.\n"


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

        cases = [
            ("an unregistered [Hosted] file", '#include "other_tests.cpp"\n', 1),
            ("a registered file with no tag", '#include "rig_tests.cpp"\n', 1),
            ("a correct registry", '#include "probe_tests.cpp"\n', 0),
        ]
        for name, body, expected in cases:
            registry.write_text(body)
            got = check(tests, registry, hush)
            status = "ok" if got == expected else "SELF-TEST FAILED"
            print("  {:34s} exit {} ({})".format(name, got, status))
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
