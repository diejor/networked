#!/usr/bin/env python
"""Hold `extension/**` to the zero-comment law, as a ratchet rather than a wall.

check_no_comments.py              # the census, newest offenders first
check_no_comments.py --ratchet    # exit 1 if any file gained a comment
check_no_comments.py --record     # rewrite the census from the tree
check_no_comments.py --strict     # exit 1 on any comment anywhere
check_no_comments.py --self-test  # proves itself red, then green
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CENSUS = Path(__file__).resolve().parent / "comment_census.txt"
SUFFIXES = (".hpp", ".cpp", ".h", ".inl")
SKIP_PARTS = ("thirdparty", "gen")
MARKER = re.compile(r"^\}\s*//\s*namespace(\s+\S+)?\s*$")


def comment_lines(text):
    """The lines carrying comment text, split into prose and scope markers."""
    lines = set()
    line = 1
    at = 0
    size = len(text)
    in_block = False
    in_string = False
    in_char = False
    while at < size:
        char = text[at]
        nxt = text[at + 1] if at + 1 < size else ""
        if char == "\n":
            line += 1
            at += 1
            if in_block:
                lines.add(line)
            in_string = False
            in_char = False
            continue
        if in_block:
            lines.add(line)
            if char == "*" and nxt == "/":
                in_block = False
                at += 2
                continue
            at += 1
            continue
        if in_string or in_char:
            if char == "\\":
                at += 2
                continue
            if (in_string and char == '"') or (in_char and char == "'"):
                in_string = False
                in_char = False
            at += 1
            continue
        if char == '"':
            in_string = True
            at += 1
            continue
        if char == "'":
            in_char = True
            at += 1
            continue
        if char == "/" and nxt == "/":
            lines.add(line)
            while at < size and text[at] != "\n":
                at += 1
            continue
        if char == "/" and nxt == "*":
            lines.add(line)
            in_block = True
            at += 2
            continue
        at += 1
    markers = set()
    body = text.split("\n")
    for number in sorted(lines):
        row = body[number - 1].strip() if number - 1 < len(body) else ""
        if MARKER.match(row):
            markers.add(number)
    return len(lines - markers), len(markers)


def sources(root):
    out = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES:
            continue
        if any(part in SKIP_PARTS for part in path.relative_to(root).parts):
            continue
        out.append(path)
    return out


def measure(root):
    counts = {}
    markers = 0
    for path in sources(root):
        prose, marked = comment_lines(path.read_text(errors="replace"))
        markers += marked
        if prose:
            counts[str(path.relative_to(root))] = prose
    return counts, markers


def read_census(census):
    rows = {}
    if not census.exists():
        return rows
    for raw in census.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        count, name = line.split(None, 1)
        rows[name] = int(count)
    return rows


def render_census(counts, markers):
    order = sorted(counts.items(), key=lambda row: (-row[1], row[0]))
    body = "".join("%d %s\n" % (count, name) for name, count in order)
    return (
        "# One row per file under extension/ that still carries prose, and\n"
        "# how many lines of it. A file may lose prose freely and may\n"
        "# never gain one: check_no_comments.py --ratchet holds this line.\n"
        "# A file absent here is expected to hold none.\n"
        "# total %d prose lines across %d files, plus %d `} // namespace`\n"
        "# scope markers the ratchet does not count and --strict does.\n" % (sum(counts.values()), len(counts), markers)
    ) + body


def check_ratchet(counts, recorded, out=print):
    refusals = []
    for name in sorted(counts):
        allowed = recorded.get(name, 0)
        if counts[name] > allowed:
            refusals.append("%s carries %d prose lines and may carry %d" % (name, counts[name], allowed))
    stale = [name for name in sorted(recorded) if recorded[name] > counts.get(name, 0)]
    for line in refusals:
        out("REFUSED %s" % line)
    for name in stale:
        out("STALE   %s is down to %d from %d; re-record the census" % (name, counts.get(name, 0), recorded[name]))
    out(
        "COMMENTS files=%d prose=%d refusals=%d stale=%d"
        % (len(counts), sum(counts.values()), len(refusals), len(stale))
    )
    return refusals


SELF_CLEAN = "int main() { return 0; }\n"
SELF_LINE = "// a note\nint main() { return 0; }\n"
SELF_BLOCK = "/* a note\n * over lines\n */\nint main() { return 0; }\n"
SELF_TRAILING = "} // namespace netw\n"
SELF_STRING = 'const char *u = "http://example.com";\nchar q = \'"\';\n'


def self_test():
    import tempfile

    failed = 0
    swallowed = []

    def hush(text):
        swallowed.append(text)

    cases = [
        ("a clean file", SELF_CLEAN, (0, 0)),
        ("a line comment", SELF_LINE, (1, 0)),
        ("a block comment", SELF_BLOCK, (3, 0)),
        ("a trailing namespace marker", SELF_TRAILING, (0, 1)),
        ("a url inside a string", SELF_STRING, (0, 0)),
    ]
    for name, body, expected in cases:
        got = comment_lines(body)
        status = "ok" if got == expected else "SELF-TEST FAILED"
        print("  {:34s} lines {} ({})".format(name, got[0], status))
        failed += 0 if got == expected else 1

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "src").mkdir()
        probe = root / "src" / "probe.cpp"

        probe.write_text(SELF_CLEAN)
        recorded = {}
        got = len(check_ratchet(measure(root)[0], recorded, hush))
        print(
            "  {:34s} refusals {} ({})".format(
                "an unlisted clean file",
                got,
                "ok" if got == 0 else "SELF-TEST FAILED",
            )
        )
        failed += 0 if got == 0 else 1

        probe.write_text(SELF_LINE)
        got = len(check_ratchet(measure(root)[0], recorded, hush))
        print(
            "  {:34s} refusals {} ({})".format(
                "an unlisted file that gained one",
                got,
                "ok" if got == 1 else "SELF-TEST FAILED",
            )
        )
        failed += 0 if got == 1 else 1

        recorded = {"src/probe.cpp": 1}
        got = len(check_ratchet(measure(root)[0], recorded, hush))
        print(
            "  {:34s} refusals {} ({})".format(
                "a listed file at its budget",
                got,
                "ok" if got == 0 else "SELF-TEST FAILED",
            )
        )
        failed += 0 if got == 0 else 1

        probe.write_text(SELF_BLOCK)
        got = len(check_ratchet(measure(root)[0], recorded, hush))
        print(
            "  {:34s} refusals {} ({})".format(
                "a listed file that grew",
                got,
                "ok" if got == 1 else "SELF-TEST FAILED",
            )
        )
        failed += 0 if got == 1 else 1

    if failed:
        print("{} self-test case(s) did not behave as declared".format(failed))
    else:
        print("self-test: the check refuses what it claims to refuse")
    return 1 if failed else 0


def main(argv):
    if "--self-test" in argv:
        return self_test()

    counts, markers = measure(ROOT)

    if "--record" in argv:
        CENSUS.write_text(render_census(counts, markers))
        print("RECORDED %d files, %d prose lines -> %s" % (len(counts), sum(counts.values()), CENSUS.name))
        return 0

    if "--strict" in argv:
        for name in sorted(counts, key=lambda row: (-counts[row], row)):
            print("REFUSED %s carries %d prose lines" % (name, counts[name]))
        print(
            "COMMENTS files=%d prose=%d markers=%d refusals=%d"
            % (
                len(counts),
                sum(counts.values()),
                markers,
                len(counts) + markers,
            )
        )
        return 1 if counts or markers else 0

    if "--ratchet" in argv:
        return 1 if check_ratchet(counts, read_census(CENSUS)) else 0

    for name in sorted(counts, key=lambda row: (-counts[row], row)):
        print("%6d %s" % (counts[name], name))
    print("COMMENTS files=%d prose=%d markers=%d" % (len(counts), sum(counts.values()), markers))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
