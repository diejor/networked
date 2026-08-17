#!/usr/bin/env python3
"""Hold a code-tap capture to the grids its own declarations state.

`code_tap.gd` records, per tick, each set's declaration and the CANONICAL
values the receiver would hold: the values after quantization, not the live
ones. So every recorded value must already sit on the grid its field declares,
and a value that does not is a canonicalization that disagrees with the
declaration it was made from. That disagreement is invisible in a running game
because both sides of the wire share the same wrong grid.

The grid comes from the declaration and nothing else. A field's step is
`2 * error` and its origin is `min_limit`, which is the arithmetic
`code_tap.gd` states in prose, so a capture is readable without the engine that
wrote it and this tool shares no code with the encoder it checks.

    code_tap_check.py netw_code_tap.jsonl
    code_tap_check.py --self-test

WHAT IT EXERCISED IS PART OF THE ANSWER. A capture whose fields declare no grid
is checkable in no respect, and it would otherwise report zero violations and
read as a pass. The counts are printed on every run for that reason: a run that
checked nothing says so on the same line as its zero.

Exits 1 on a value off its grid, on a malformed capture, or on a run that
checked nothing.
"""
import argparse
import json
import sys

# A canonical value has been through a 32-bit float, so it is the f32 NEAREST
# its grid point rather than the grid point itself. How far that is, measured
# in steps, grows with the value's magnitude and shrinks with the step: a
# coarse grid hides the error and a fine one at a large coordinate does not.
# Checking against a fixed fraction of a step therefore reddens every fine
# grid far from its origin, which is a real capture's normal condition.
#
# MEASURED 2026-08-11 over a racing capture: sphere_position on a 0.0078 grid
# at coordinate 4.66 sits 1.9e-5 steps off, and 1.2e-7 * 4.66 / 0.0078 is
# 7.2e-5. The headroom is for the rounding an encode and a decode each add.
FLOAT32_EPSILON = 1.2e-7
STEP_TOLERANCE = 1e-9


def step_tolerance(value, step):
    """How far off a grid an f32 of this magnitude is allowed to land."""
    return max(STEP_TOLERANCE, 4.0 * abs(value) * FLOAT32_EPSILON / step)


class Malformed(Exception):
    pass


def axes_of(value):
    """A recorded value as the axes its quantizer grids independently."""
    if isinstance(value, bool) or value is None:
        return []
    if isinstance(value, (int, float)):
        return [float(value)]
    if isinstance(value, list):
        out = []
        for item in value:
            out.extend(axes_of(item))
        return out
    return []


def grid_of(field):
    """(origin, step, low, high) for a field that declares one, else None."""
    if "error" not in field or "min_limit" not in field:
        return None
    error = float(field["error"])
    if error <= 0.0:
        return None
    low = float(field["min_limit"])
    high = float(field["max_limit"]) if "max_limit" in field else None
    return (low, 2.0 * error, low, high)


def check(lines, out=print):
    declarations = {}
    rows = fields_seen = axes_checked = ungridded = 0
    violations = []

    for number, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except ValueError as broken:
            raise Malformed("line %d is not JSON: %s" % (number, broken))
        if "fields" in entry:
            declarations[entry.get("decl")] = entry["fields"]
            continue
        if "values" not in entry:
            raise Malformed("line %d is neither a declaration nor a row" % number)

        key = entry.get("decl")
        if key not in declarations:
            raise Malformed(
                "line %d holds a row for %r, which nothing declared"
                % (number, key)
            )
        declared = declarations[key]
        values = entry["values"]
        if len(values) != len(declared):
            raise Malformed(
                "line %d holds %d value(s) for a declaration of %d field(s)"
                % (number, len(values), len(declared))
            )
        rows += 1

        for field, value in zip(declared, values):
            fields_seen += 1
            grid = grid_of(field)
            if grid is None:
                ungridded += 1
                continue
            origin, step, low, high = grid
            for axis in axes_of(value):
                axes_checked += 1
                if axis < low - step or (high is not None and axis > high + step):
                    violations.append(
                        "%s.%s tick %s: %r is outside its declared limits "
                        "[%g, %g]"
                        % (key, field.get("key"), entry.get("tick"), axis,
                           low, high if high is not None else float("inf"))
                    )
                    continue
                steps = (axis - origin) / step
                if abs(steps - round(steps)) > step_tolerance(axis, step):
                    violations.append(
                        "%s.%s tick %s: %r is %.6f steps off a grid of %g "
                        "from %g"
                        % (key, field.get("key"), entry.get("tick"), axis,
                           abs(steps - round(steps)), step, origin)
                    )

    out(
        "TAP rows=%d fields=%d axes_checked=%d ungridded=%d violations=%d"
        % (rows, fields_seen, axes_checked, ungridded, len(violations))
    )
    for line in violations:
        out("OFF-GRID " + line)
    if axes_checked == 0:
        out(
            "REFUSED nothing in this capture declares a grid, so it was "
            "checked in no respect"
        )
        return 1
    return 1 if violations else 0


DECL = {
    "decl": "pos:0",
    "record": 0,
    "fields": [
        {"key": "position", "lane": 0, "packer": "NetwQuantizeBits",
         "bits": 38, "error": 0.5, "min_limit": -512.0, "max_limit": 512.0},
    ],
}


def row(tick, value):
    return {"decl": "pos:0", "tick": tick, "route": 7, "values": [value]}


def self_test():
    """Proves the check red before it is trusted green."""
    wrong = 0

    def case(name, lines, expected):
        nonlocal wrong
        swallowed = []
        try:
            got = check([json.dumps(line) for line in lines],
                        out=swallowed.append)
        except Malformed:
            got = 1
        ok = got == expected
        wrong += 0 if ok else 1
        print("%s %-52s exit=%d expected=%d"
              % ("ok  " if ok else "FAIL", name, got, expected))

    # A step of 2 * 0.5 = 1.0 from an origin of -512, so every integer is a
    # grid point and nothing between two of them is.
    case("values on their declared grid", [DECL, row(1, [1.0, -2.0])], 0)
    case("a value between two grid points", [DECL, row(1, [1.5, 0.0])], 1)
    case("a value past its declared maximum", [DECL, row(1, [9000.0, 0.0])], 1)
    case("a row for a declaration nobody wrote",
         [row(1, [1.0, 0.0])], 1)
    case("a row whose width is not its declaration's",
         [DECL, {"decl": "pos:0", "tick": 1, "route": 7,
                 "values": [[1.0], [2.0]]}], 1)

    # The one a violation count cannot catch: a capture nothing in which is
    # gridded reports zero violations and has checked nothing.
    bare = {"decl": "raw:0", "record": 0,
            "fields": [{"key": "name", "lane": 0}]}
    case("a capture that declares no grid at all",
         [bare, {"decl": "raw:0", "tick": 1, "route": 7, "values": [3.0]}], 1)

    print("SELFTEST %d wrong" % wrong)
    return 1 if wrong else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.capture:
        parser.error("a capture path is required without --self-test")
    with open(args.capture, "r", errors="replace") as handle:
        try:
            return check(handle.readlines())
        except Malformed as broken:
            sys.exit("MALFORMED %s" % broken)


if __name__ == "__main__":
    sys.exit(main())
