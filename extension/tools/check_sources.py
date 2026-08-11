#!/usr/bin/env python
"""Fail if a C++ source in the tree is not one the build compiles.

The manifest globs today, so this passes by construction. That is the point:
it is what stops the manifest from quietly becoming a hand-written list that
a new file gets left out of, which is the one way a source can exist, compile
locally through a stale object, and be missing from a clean build.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import sources  # noqa: E402


def main():
    compiled = {path.resolve() for path in sources.source_paths(netw_tests=True)}
    present = {path.resolve() for path in sources.ROOT.glob("*.cpp")}
    for directory in ("src", "tests"):
        present |= {path.resolve() for path in (sources.ROOT / directory).rglob("*.cpp")}

    missing = sorted(present - compiled)
    for path in missing:
        print("not compiled by any build entry: {}".format(path))
    if missing:
        return 1

    absent = sorted(path for path in compiled if not path.exists())
    for path in absent:
        print("manifest names a file that does not exist: {}".format(path))
    if absent:
        return 1

    print("{} sources, all compiled".format(len(compiled)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
