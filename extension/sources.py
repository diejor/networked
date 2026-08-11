#!/usr/bin/env python

from pathlib import Path

ROOT = Path(__file__).resolve().parent

# The two build entries, named by the gate macro each one defines.
GDEXTENSION = "NETW_GDEXTENSION"
MODULE = "NETW_MODULE"

# The optional feature gates, spelled once so both entries agree.
PROFILING = "NETW_PROFILING"
TESTS = "NETW_TESTS"


def source_paths(netw_tests=False):
    """Every translation unit both build entries compile, in a fixed order.

    Deliberately a glob rather than a list: the two entries import this one
    function, so a file that exists is a file that builds, and there is no
    manifest to fall out of step with the tree. tools/check_sources.py holds
    that property to the rule.
    """
    paths = sorted((ROOT / "src").rglob("*.cpp"))
    paths += [ROOT / "register_types.cpp"]
    if netw_tests:
        paths += sorted((ROOT / "tests").rglob("*.cpp"))
    return paths


def get_sources(env, editor_build):
    del editor_build
    return [env.File(str(path)) for path in source_paths(env.get("netw_tests", False))]


# Flags that change what floating-point arithmetic ANSWERS, spelled for every
# compiler the two entries are built with.
#
# The determinism floor these defend is a measured, single-platform,
# single-engine-build, single-Jolt-build result: four OS processes produced
# byte-identical float32 traces, and that measurement is what licenses a
# byte-diffed golden and a var_to_bytes shadow to certify a port at all. A flag
# here does not fail a case, it invalidates the instrument, because the
# GDScript arm the native arm is diffed against contracts nothing and rounds
# under IEEE rules it cannot be asked to relax.
_FLOOR_BANNED = (
    "-ffast-math",
    "-funsafe-math-optimizations",
    "-fassociative-math",
    "-freciprocal-math",
    "-ffinite-math-only",
    "-fno-signed-zeros",
    "/fp:fast",
)

# Contraction is off rather than merely unbanned. A fused multiply-add is a
# different answer from a multiply and an add, and the arm on the other side of
# the diff performs two operations.
_FLOOR_REQUIRED = {
    "msvc": ["/fp:precise"],
    "posix": ["-ffp-contract=off"],
}

_FLOOR_FLAG_KEYS = ("CCFLAGS", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LINKFLAGS")


def _floor_offender(env, arguments):
    """The (source, flag) a build would answer arithmetic differently under.

    Three sources rather than one, because a flag arrives by three doors and a
    guard that watches only the environment reads green against the other two:
    an SCons variable assignment, the shell's own CXXFLAGS, and a flag list
    something already appended.
    """
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
    """Refuses a build whose flags would answer arithmetic differently.

    `refuse` is called with the message and must not return.
    """
    offender = _floor_offender(env, arguments or {})
    if offender is not None:
        key, banned = offender
        refuse(
            "%s carries %s, which changes what floating-point arithmetic "
            "answers. The prediction family certifies a port by byte-diffing "
            "a golden against a GDScript arm, so this invalidates the "
            "instrument rather than failing a case." % (key, banned)
        )
        return
    dialect = "msvc" if str(env.get("CC", "")).endswith("cl") else "posix"
    env.Append(CCFLAGS=_FLOOR_REQUIRED[dialect])


def feature_defines(entry, netw_profiling, netw_tests):
    """The preprocessor gates for one build entry.

    `entry` is GDEXTENSION or MODULE.
    """
    if entry not in (GDEXTENSION, MODULE):
        raise ValueError("entry must be sources.GDEXTENSION or sources.MODULE")
    defines = [entry]
    if netw_profiling:
        defines.append(PROFILING)
    if netw_tests:
        defines.append(TESTS)
    return defines
