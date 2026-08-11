#!/usr/bin/env python

"""The entry point the engine's module scanner looks for.

The engine names a module after the directory it sits in, so this one is
mounted as a symlink or junction called `networked` inside the engine's
`modules/`. BUILDING.md carries the command.
"""

from pathlib import Path

import sources

ROOT = Path(__file__).resolve().parent
DOC_PATH = "doc_classes"


def can_build(env, platform):
    return True


def configure(env):
    """Put this module's headers and NETW_MODULE on the engine's environment.

    Not in `SCsub`, because the engine compiles the headers under `tests/` in
    an environment `SCsub` never reaches. Keep this to settings that never
    vary: they reach every engine source, so a change rebuilds all of them.
    """
    env.Append(CPPPATH=[str(ROOT), str(ROOT / "include")])
    env.Append(CPPDEFINES=[sources.MODULE])


def get_doc_classes():
    """Every documented class, read from the XML rather than listed."""
    return sorted(path.stem for path in (ROOT / DOC_PATH).glob("*.xml"))


def get_doc_path():
    return DOC_PATH
