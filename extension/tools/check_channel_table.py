#!/usr/bin/env python
"""Fail if the GDScript channel enum and the native registry are two tables.

`NetwFrameEnvelope.Channel` names what the shell puts in a frame's channel byte.
`netw::wire::WireRegistry::create_default()` declares what each of those bytes
MEANS: its kind, its reliability, its freshness, its delivery and its payload
contract. Every carried payload rides behind one of those declarations, so the
two have to be one table.

Nothing else notices when they are not. An id the shell sends and the registry
does not declare is an undeclared payload that still crosses, and the wire
identity hash folds only what the registry holds, so both builds compute the
SAME app tag while disagreeing about the wire. That is the campaign's recurring
shape: a failure path answering with a plausible value instead of a refusal.

Three ways they can disagree, each a different lie:

  an id in the enum that the registry does not declare is a payload crossing
  with no contract behind it;

  an id the registry declares that the enum does not name is a declaration no
  sender can reach, which usually means a channel was renamed on one side;

  the same id under two names is the worst of the three, because both sides
  believe they agree.

Reserved ids are checked the other way round. A retired carrier is reserved so
it is never reclaimed, and an enum that names one is a build about to hand a
dead id to a live family.

    check_channel_table.py --spec reports/native/wire.spec.json
    check_channel_table.py --self-test   # proves itself red, then green

The spec is written by the native runner from the registry the build actually
holds, never parsed out of the C++, because a table read from source is not the
table that shipped.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENVELOPE = ROOT / "addons/networked/replication/netw_frame_envelope.gd"

MEMBER = re.compile(r"^\t([A-Z][A-Z0-9_]*) = (\d+),", re.MULTILINE)


def enum_members(text):
    """The `Channel` enum's members as {id: name}, ignoring every other enum."""
    start = text.find("enum Channel {")
    if start < 0:
        return {}
    body = text[start:]
    body = body[: body.index("\n}")]
    return {int(value): name for name, value in MEMBER.findall(body)}


def check(members, spec, out=print):
    declared = {int(row["id"]): str(row["name"]) for row in spec.get("channels", [])}
    reserved = {int(value) for value in spec.get("reserved", [])}
    refusals = []

    for id_, name in sorted(members.items()):
        if id_ in reserved:
            refusals.append(
                "enum names %s = %d, which the registry reserves as retired"
                % (name, id_)
            )
        elif id_ not in declared:
            refusals.append(
                "enum names %s = %d, which the registry does not declare"
                % (name, id_)
            )
        elif declared[id_] != name:
            refusals.append(
                "id %d is %s in the enum and %s in the registry"
                % (id_, name, declared[id_])
            )

    for id_, name in sorted(declared.items()):
        if id_ not in members:
            refusals.append(
                "registry declares %s = %d, which the enum does not name"
                % (name, id_)
            )

    for line in refusals:
        out("REFUSED %s" % line)
    out(
        "CHANNELS enum=%d declared=%d reserved=%d refusals=%d"
        % (len(members), len(declared), len(reserved), len(refusals))
    )
    return refusals


SPEC = {
    "channels": [
        {"id": 2, "name": "ACTION"},
        {"id": 3, "name": "CALL"},
    ],
    "reserved": [1],
}

ENUM_OK = "enum Channel {\n\tACTION = 2,\n\tCALL = 3,\n}\n"
ENUM_UNDECLARED = "enum Channel {\n\tACTION = 2,\n\tCALL = 3,\n\tGHOST = 4,\n}\n"
ENUM_MISSING = "enum Channel {\n\tACTION = 2,\n}\n"
ENUM_RENAMED = "enum Channel {\n\tACTION = 2,\n\tSHOUT = 3,\n}\n"
ENUM_RECLAIMED = "enum Channel {\n\tACTION = 2,\n\tCALL = 3,\n\tREBORN = 1,\n}\n"

# A second enum in the same file must not be read as this one. The envelope
# really does carry others, and counting their members would refuse a table
# that agrees.
ENUM_NEIGHBOUR = ENUM_OK + "\nenum Other {\n\tSOMETHING = 9,\n}\n"


def self_test():
    hush = lambda *args, **kwargs: None
    cases = [
        ("a table both sides agree on", ENUM_OK, 0),
        ("an enum id nothing declares", ENUM_UNDECLARED, 1),
        ("a declaration no enum names", ENUM_MISSING, 1),
        ("one id under two names", ENUM_RENAMED, 1),
        ("a reserved id reclaimed", ENUM_RECLAIMED, 1),
        ("a neighbouring enum in the same file", ENUM_NEIGHBOUR, 0),
    ]
    failed = 0
    for name, text, expected in cases:
        got = len(check(enum_members(text), SPEC, out=hush))
        ok = got == expected
        failed += 0 if ok else 1
        print(
            "%-4s %-40s refusals=%d expected=%d"
            % ("ok" if ok else "WRONG", name, got, expected)
        )
    print("SELFTEST %d wrong" % failed)
    return 1 if failed else 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    spec_path = None
    if "--spec" in argv:
        spec_path = Path(argv[argv.index("--spec") + 1])
    if spec_path is None or not spec_path.is_file():
        print("REFUSED no wire spec at %s" % spec_path)
        return 1
    spec = json.loads(spec_path.read_text())
    if "channels" not in spec:
        print("REFUSED %s carries no channel table" % spec_path)
        return 1
    members = enum_members(ENVELOPE.read_text(errors="replace"))
    if not members:
        print("REFUSED no Channel enum in %s" % ENVELOPE)
        return 1
    return 1 if check(members, spec) else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
