#!/usr/bin/env python

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent

GDEXTENSION = "NETW_GDEXTENSION"
MODULE = "NETW_MODULE"

PROFILING = "NETW_PROFILING"
TESTS = "NETW_TESTS"


def source_paths(netw_tests=False):
    """Every translation unit both build entries compile, in a fixed order."""
    paths = sorted((ROOT / "src").rglob("*.cpp"))
    paths += [ROOT / "register_types.cpp"]
    if netw_tests:
        paths += sorted((ROOT / "tests").rglob("*.cpp"))
    return paths


def get_sources(env, editor_build):
    del editor_build
    return [env.File(str(path)) for path in source_paths(env.get("netw_tests", False))]


HOSTED_MANIFEST = ROOT / "tests" / "test_networked_hosted.gen.h"

_HOSTED_TAG = re.compile(r'"\[Networked\]\[[A-Za-z0-9]+\]\[Hosted\]')

_HOSTED_PROLOGUE = """#pragma once

#include "support/netw_cells.h"
#include "support/netw_reset.h"

"""

_HOSTED_EPILOGUE = """
NETW_INSTALL_RESET_LISTENER();
NETW_INSTALL_CELLS_LISTENER();
"""


def hosted_case_files():
    """Every case file under `tests/` that declares itself tier-portable."""
    return sorted(
        path.name for path in (ROOT / "tests").rglob("*.cpp") if _HOSTED_TAG.search(path.read_text(errors="replace"))
    )


def hosted_manifest_text():
    includes = "".join('#include "%s"\n' % name for name in hosted_case_files())
    return _HOSTED_PROLOGUE + includes + _HOSTED_EPILOGUE


def write_hosted_manifest():
    """Rewrite the manifest from the tree, and answer whether it changed."""
    text = hosted_manifest_text()
    if HOSTED_MANIFEST.is_file() and HOSTED_MANIFEST.read_text() == text:
        return False
    HOSTED_MANIFEST.write_text(text, encoding="utf-8")
    return True


_FLOOR_BANNED = (
    "-ffast-math",
    "-funsafe-math-optimizations",
    "-fassociative-math",
    "-freciprocal-math",
    "-ffinite-math-only",
    "-fno-signed-zeros",
    "/fp:fast",
)

_FLOOR_REQUIRED = {
    "msvc": ["/fp:precise"],
    "posix": ["-ffp-contract=off"],
}

_FLOOR_FLAG_KEYS = ("CCFLAGS", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LINKFLAGS")


def _floor_offender(env, arguments):
    """The (source, flag) a build would answer arithmetic differently under."""
    import os

    haystacks = [(key, " ".join(str(f) for f in env.get(key, []))) for key in _FLOOR_FLAG_KEYS]
    haystacks += [(key, str(arguments.get(key, ""))) for key in _FLOOR_FLAG_KEYS]
    haystacks += [(key, os.environ.get(key, "")) for key in _FLOOR_FLAG_KEYS]
    for key, text in haystacks:
        for banned in _FLOOR_BANNED:
            if banned in text:
                return key, banned
    return None


def hold_determinism_floor(env, refuse, arguments=None):
    """Refuses a build whose flags would answer arithmetic differently."""
    offender = _floor_offender(env, arguments or {})
    if offender is not None:
        key, banned = offender
        refuse(
            "%s carries %s, which changes what floating-point arithmetic "
            "answers. The prediction family holds determinism by comparing "
            "replayed float traces byte for byte, so this invalidates the "
            "instrument rather than failing a case." % (key, banned)
        )
        return
    dialect = "msvc" if str(env.get("CC", "")).endswith("cl") else "posix"
    env.Append(CCFLAGS=_FLOOR_REQUIRED[dialect])


def feature_defines(entry, netw_profiling, netw_tests):
    """The preprocessor gates for one build entry."""
    if entry not in (GDEXTENSION, MODULE):
        raise ValueError("entry must be sources.GDEXTENSION or sources.MODULE")
    defines = [entry]
    if netw_profiling:
        defines.append(PROFILING)
    if netw_tests:
        defines.append(TESTS)
    return defines
