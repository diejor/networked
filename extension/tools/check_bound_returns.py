#!/usr/bin/env python
"""Refuse a ClassDB-bound accessor that answers a refcounted object by pointer.

A method returning `T *` is correct for every C++ caller and wrong for every
scripted one when `T` is reference-counted: the Variant handed across the
boundary never took a reference, so the object is freed under the caller and
the next read answers null. It compiles, it links, and it passes the whole
native tier, because that tier calls the C++ directly. Only a GDScript caller
finds out, and it finds out as a stall or a crash somewhere else.

The rule: a bound method whose declared return type is a pointer to a class
deriving from RefCounted or Resource must return `Ref<T>` instead. Raw pointers
stay legal for the unbound C++ form, so the fix is a wrapper, not a rewrite.

    check_bound_returns.py            # the refusals, with the class chain
    check_bound_returns.py --census   # every bound Object * return, unjudged
    check_bound_returns.py --self-test

`Object *` is reported by `--census` rather than refused: it is the correct
return for a Node, which is not refcounted, and refusing it would ban the
common case to catch the rare one.
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = (ROOT / "include", ROOT / "src", ROOT)
SKIP_PARTS = ("thirdparty", "gen", "tests")

REFCOUNTED_ROOTS = {"RefCounted", "Resource"}

GDCLASS = re.compile(r"\bGDCLASS\s*\(\s*([\w]+)\s*,\s*(?:godot::)?([\w]+)\s*\)")
BOUND = re.compile(
    r"bind(?:_static)?_method\s*\([^;]*?&\s*(?:netw::)?([\w]+)::([\w]+)\s*[,)]",
    re.S,
)


def sources(roots):
    seen = set()
    for root in roots:
        for suffix in ("*.hpp", "*.cpp", "*.h", "*.inl"):
            for path in root.rglob(suffix):
                if any(part in SKIP_PARTS for part in path.parts):
                    continue
                if path not in seen:
                    seen.add(path)
                    yield path


def read_all(roots):
    return {path: path.read_text(errors="replace") for path in sources(roots)}


def class_bases(texts):
    bases = {}
    for text in texts.values():
        for name, base in GDCLASS.findall(text):
            bases[name] = base
    return bases


def refcounted(name, bases):
    """Whether `name` reaches RefCounted or Resource through its GDCLASS chain."""
    seen = set()
    while name and name not in seen:
        if name in REFCOUNTED_ROOTS:
            return True
        seen.add(name)
        name = bases.get(name)
    return False


def return_type(texts, class_name, method):
    """The declared return type of `class_name::method`, as (type, is_pointer).

    Read from the declaration rather than the definition, because most of these
    are inline in the class body and never have an out-of-line form.
    """
    pattern = re.compile(
        r"(?m)^[ \t]*(?:static[ \t]+|virtual[ \t]+)*"
        r"([A-Za-z_][\w:]*(?:<[^>]*>)?)[ \t]*(\*?)[ \t]*"
        + re.escape(method)
        + r"[ \t]*\("
    )
    for text in texts.values():
        if class_name not in text:
            continue
        for found in pattern.finditer(text):
            spelling = found.group(1)
            if spelling in ("return", "if", "while", "for", "switch"):
                continue
            return spelling, found.group(2) == "*"
    return None, False


def check(roots=SCAN_ROOTS, out=print, census=False):
    texts = read_all(roots)
    bases = class_bases(texts)

    refusals = []
    objects = []
    for path, text in texts.items():
        for class_name, method in set(BOUND.findall(text)):
            spelling, pointer = return_type(texts, class_name, method)
            if not pointer:
                continue
            plain = spelling.split("::")[-1]
            if plain == "Object":
                objects.append((path, class_name, method))
            elif refcounted(plain, bases):
                refusals.append((path, class_name, method, plain))

    if census:
        for path, class_name, method in sorted(
            objects, key=lambda row: (str(row[0]), row[1], row[2])
        ):
            out("{}: {}::{} answers Object *".format(
                path.relative_to(ROOT) if path.is_relative_to(ROOT) else path,
                class_name,
                method,
            ))
        out("{} bound method(s) answer Object *".format(len(objects)))
        return 0

    for path, class_name, method, spelling in sorted(
        refusals, key=lambda row: (str(row[0]), row[1], row[2])
    ):
        out(
            "{}: {}::{} answers {} * to a script; return Ref<{}>".format(
                path.relative_to(ROOT) if path.is_relative_to(ROOT) else path,
                class_name,
                method,
                spelling,
                spelling,
            )
        )
    if refusals:
        return 1
    out("bound accessors answer refcounted objects by reference")
    return 0


def self_test():
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "src").mkdir()
        header = root / "src" / "probe.hpp"
        body = root / "src" / "probe.cpp"

        header.write_text(
            "class NetwThing : public RefCounted {\n"
            "    GDCLASS(NetwThing, RefCounted)\n"
            "public:\n"
            "    NetwThing *held();\n"
            "};\n"
            "class NetwHolder : public RefCounted {\n"
            "    GDCLASS(NetwHolder, RefCounted)\n"
            "public:\n"
            "    NetwThing *thing();\n"
            "};\n"
        )
        body.write_text(
            'void NetwHolder::_bind_methods() {\n'
            '    ClassDB::bind_method(D_METHOD("thing"), &NetwHolder::thing);\n'
            "}\n"
        )
        if check((root / "src",), lambda _line: None) != 1:
            print("self-test failed to refuse a raw refcounted return")
            return 1

        header.write_text(
            "class NetwThing : public RefCounted {\n"
            "    GDCLASS(NetwThing, RefCounted)\n"
            "};\n"
            "class NetwHolder : public RefCounted {\n"
            "    GDCLASS(NetwHolder, RefCounted)\n"
            "public:\n"
            "    Ref<NetwThing> thing();\n"
            "};\n"
        )
        if check((root / "src",), lambda _line: None) != 0:
            print("self-test refused the referenced return")
            return 1

        header.write_text(
            "class NetwHolder : public RefCounted {\n"
            "    GDCLASS(NetwHolder, RefCounted)\n"
            "public:\n"
            "    Node *owner();\n"
            "};\n"
        )
        body.write_text(
            'void NetwHolder::_bind_methods() {\n'
            '    ClassDB::bind_method(D_METHOD("owner"), &NetwHolder::owner);\n'
            "}\n"
        )
        if check((root / "src",), lambda _line: None) != 0:
            print("self-test refused a node return, which is not refcounted")
            return 1

    print("self-test: the check refuses a refcounted object answered by pointer")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--census", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    return check(census=args.census)


if __name__ == "__main__":
    sys.exit(main())
