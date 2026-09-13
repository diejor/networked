#!/usr/bin/env python

"""The entry point the engine's module scanner looks for."""

from pathlib import Path

import sources

ROOT = Path(__file__).resolve().parent
DOC_PATH = "doc_classes"


def can_build(env, platform):
    return True


def configure(env):
    """Put this module's headers and NETW_MODULE on the engine's environment."""
    env.Append(CPPPATH=[str(ROOT), str(ROOT / "include")])
    env.Append(CPPDEFINES=[sources.MODULE])
    sources.write_hosted_manifest()


def get_doc_classes():
    """Every documented class, read from the XML rather than listed."""
    return sorted(path.stem for path in (ROOT / DOC_PATH).glob("*.xml"))


def get_doc_path():
    return DOC_PATH
