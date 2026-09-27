#!/usr/bin/env python3
"""Generate the C# bindings the addon ships, from the engine's own API dump."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path

import csharp_docs
from common import CI_DIR, ROOT, fail, load_json, main, run

DUMP_NAME = "extension_api.json"
OUTPUT_DIR = ROOT / "addons" / "networked" / "native_api" / "cs"
REGISTRY = ROOT / "extension" / "register_types.cpp"
HAND_WRITTEN = {"NetwApi.cs", "NetwObject.cs"}
THUNKS_NAME = "NetwThunks.cs"
DOC_DIR = ROOT / "extension" / "doc_classes"
NAMES_FILE = CI_DIR / "engine_names.json"

REGISTRATION = re.compile(
    r"GDREGISTER_(?:ABSTRACT_|RUNTIME_|VIRTUAL_)?CLASS\(" r"(?:[A-Za-z0-9_]+::)*([A-Za-z0-9_]+)\s*\)"
)

TEST_GUARD = "NETW_TESTS"

KEYWORDS = {
    "abstract",
    "as",
    "base",
    "bool",
    "break",
    "byte",
    "case",
    "catch",
    "char",
    "checked",
    "class",
    "const",
    "continue",
    "decimal",
    "default",
    "delegate",
    "do",
    "double",
    "else",
    "enum",
    "event",
    "explicit",
    "extern",
    "false",
    "finally",
    "fixed",
    "float",
    "for",
    "foreach",
    "goto",
    "if",
    "implicit",
    "in",
    "int",
    "interface",
    "internal",
    "is",
    "lock",
    "long",
    "namespace",
    "new",
    "null",
    "object",
    "operator",
    "out",
    "override",
    "params",
    "private",
    "protected",
    "public",
    "readonly",
    "ref",
    "return",
    "sbyte",
    "sealed",
    "short",
    "sizeof",
    "stackalloc",
    "static",
    "string",
    "struct",
    "switch",
    "this",
    "throw",
    "true",
    "try",
    "typeof",
    "uint",
    "ulong",
    "unchecked",
    "unsafe",
    "ushort",
    "using",
    "virtual",
    "void",
    "volatile",
    "while",
}

RESERVED = {
    "Adopt",
    "Checked",
    "Connect",
    "Disconnect",
    "Dispose",
    "Equals",
    "Finalize",
    "From",
    "GetHashCode",
    "GetType",
    "IsConnected",
    "IsValid",
    "MemberwiseClone",
    "Native",
    "ToString",
}

GLOBAL_ENUMS = {"Error"}

VARARG_PARAMETER = "rest"

PTRCALL_MAX_ARITY = 3
CALL_MAX_ARITY = 4


def slot_tag(slot: str) -> str:
    """One C# slot type as a name fragment, so a delegate's name states its
    own signature."""
    return slot[:1].upper() + slot[1:]


def returned_slot(slot: str) -> str:
    """The slot a return value occupies. A void method still hands the thunk
    somewhere to write, because the engine's ptrcall ignores it and a `ref`
    parameter cannot be null."""
    return "long" if slot == "void" else slot


class Thunks:
    """The fixed-arity entry points the generated bodies reach, collected as
    they are emitted so the registry declares exactly the ones in use."""

    def __init__(self) -> None:
        self.ptrcalls: set[tuple[tuple[str, ...], str]] = set()
        self.calls: set[int] = set()
        self.packed = False

    def ptrcall(self, slots: list[str], answered: str) -> str:
        if len(slots) > PTRCALL_MAX_ARITY:
            raise fail("a ptrcall of arity %d has no thunk, so it belongs on the " "call path" % len(slots))
        shape = (tuple(slots), returned_slot(answered))
        self.ptrcalls.add(shape)
        return self.ptrcall_name(*shape)

    def call(self, arity: int) -> str:
        if arity > CALL_MAX_ARITY:
            raise fail("a call of arity %d belongs in a pack" % arity)
        self.calls.add(arity)
        return "Call%d" % arity

    def pack(self) -> None:
        self.packed = True

    @staticmethod
    def ptrcall_name(slots: tuple[str, ...], answered: str) -> str:
        return "Ptrcall%d_%s" % (
            len(slots),
            "_".join(slot_tag(one) for one in (*slots, answered)),
        )


@dataclass(frozen=True)
class Encoded:
    """How one API type crosses a ptrcall."""

    declared: str
    slot: str
    to_slot: str
    from_slot: str


@dataclass(frozen=True)
class Boxed:
    """How one API type crosses a Variant call."""

    declared: str
    to_variant: str
    from_variant: str


INTEGER_SLOTS = {
    "int32": Encoded("int", "int", "{name}", "{name}"),
    "uint32": Encoded("uint", "uint", "{name}", "{name}"),
    "int16": Encoded("short", "short", "{name}", "{name}"),
    "uint16": Encoded("ushort", "ushort", "{name}", "{name}"),
    "int8": Encoded("sbyte", "sbyte", "{name}", "{name}"),
    "uint8": Encoded("byte", "byte", "{name}", "{name}"),
    "int64": Encoded("long", "long", "{name}", "{name}"),
    "uint64": Encoded("ulong", "ulong", "{name}", "{name}"),
}

BLITTABLE = {
    "bool": Encoded("bool", "byte", "{name} ? (byte)1 : (byte)0", "{name} != 0"),
    "float": Encoded("double", "double", "{name}", "{name}"),
    "int": INTEGER_SLOTS["int64"],
    "Vector2i": Encoded("Vector2I", "Vector2I", "{name}", "{name}"),
    "Rect2": Encoded("Rect2", "Rect2", "{name}", "{name}"),
    "Rect2i": Encoded("Rect2I", "Rect2I", "{name}", "{name}"),
    "RID": Encoded("Rid", "Rid", "{name}", "{name}"),
}


def integer_box(declared: str, read: str, cast: str) -> Boxed:
    return Boxed(
        declared,
        "VariantUtils.CreateFromInt((%s){name})" % cast,
        "VariantUtils.%s({name})" % read,
    )


INTEGER_BOXES = {
    "int8": integer_box("sbyte", "ConvertToInt8", "long"),
    "int16": integer_box("short", "ConvertToInt16", "long"),
    "int32": integer_box("int", "ConvertToInt32", "long"),
    "int64": integer_box("long", "ConvertToInt64", "long"),
    "uint8": integer_box("byte", "ConvertToUInt8", "ulong"),
    "uint16": integer_box("ushort", "ConvertToUInt16", "ulong"),
    "uint32": integer_box("uint", "ConvertToUInt32", "ulong"),
    "uint64": integer_box("ulong", "ConvertToUInt64", "ulong"),
}


def simple_box(declared: str, made: str, read: str) -> Boxed:
    return Boxed(
        declared,
        "VariantUtils.CreateFrom%s({name})" % made,
        "VariantUtils.ConvertTo%s({name})" % read,
    )


def packed_box(declared: str, kind: str) -> Boxed:
    return Boxed(
        declared,
        "VariantUtils.CreateFromPacked%sArray({name})" % kind,
        "VariantUtils.ConvertAsPacked%sArrayToSystemArray({name})" % kind,
    )


BUILTIN_BOXES = {
    "bool": simple_box("bool", "Bool", "Bool"),
    "float": simple_box("double", "Float", "Float64"),
    "String": simple_box("string", "String", "String"),
    "StringName": simple_box("StringName", "StringName", "StringName"),
    "NodePath": simple_box("NodePath", "NodePath", "NodePath"),
    "RID": simple_box("Rid", "Rid", "Rid"),
    "Callable": simple_box("Callable", "Callable", "Callable"),
    "Signal": simple_box("Signal", "Signal", "Signal"),
    "Color": simple_box("Color", "Color", "Color"),
    "Plane": simple_box("Plane", "Plane", "Plane"),
    "Quaternion": simple_box("Quaternion", "Quaternion", "Quaternion"),
    "AABB": simple_box("Aabb", "Aabb", "Aabb"),
    "Basis": simple_box("Basis", "Basis", "Basis"),
    "Transform2D": simple_box("Transform2D", "Transform2D", "Transform2D"),
    "Transform3D": simple_box("Transform3D", "Transform3D", "Transform3D"),
    "Projection": simple_box("Projection", "Projection", "Projection"),
    "Rect2": simple_box("Rect2", "Rect2", "Rect2"),
    "Rect2i": simple_box("Rect2I", "Rect2I", "Rect2I"),
    "Vector2": simple_box("Vector2", "Vector2", "Vector2"),
    "Vector2i": simple_box("Vector2I", "Vector2I", "Vector2I"),
    "Vector3": simple_box("Vector3", "Vector3", "Vector3"),
    "Vector3i": simple_box("Vector3I", "Vector3I", "Vector3I"),
    "Vector4": simple_box("Vector4", "Vector4", "Vector4"),
    "Vector4i": simple_box("Vector4I", "Vector4I", "Vector4I"),
    "Dictionary": simple_box("Godot.Collections.Dictionary", "Dictionary", "Dictionary"),
    "Array": simple_box("Godot.Collections.Array", "Array", "Array"),
    "PackedByteArray": packed_box("byte[]", "Byte"),
    "PackedInt32Array": packed_box("int[]", "Int32"),
    "PackedInt64Array": packed_box("long[]", "Int64"),
    "PackedFloat32Array": packed_box("float[]", "Float32"),
    "PackedFloat64Array": packed_box("double[]", "Float64"),
    "PackedStringArray": packed_box("string[]", "String"),
    "PackedVector2Array": packed_box("Vector2[]", "Vector2"),
    "PackedVector3Array": packed_box("Vector3[]", "Vector3"),
    "PackedVector4Array": packed_box("Vector4[]", "Vector4"),
    "PackedColorArray": packed_box("Color[]", "Color"),
    "Variant": Boxed(
        "Variant",
        "{name}.CopyNativeVariant()",
        "Variant.CreateCopyingBorrowed({name})",
    ),
}

ENGINE_NAMES = {
    "Object": "GodotObject",
    "MultiplayerAPI": "MultiplayerApi",
}

BUILTIN_CREFS = {name: boxed.declared for name, boxed in BUILTIN_BOXES.items()}


WIDTH = 80


def split_arguments(text: str) -> list[str]:
    """The comma-separated top-level pieces of one argument list."""
    pieces = []
    depth = 0
    start = 0
    for index, letter in enumerate(text):
        if letter in "([":
            depth += 1
        elif letter in ")]":
            depth -= 1
        elif letter == "," and depth == 0:
            pieces.append(text[start:index].strip())
            start = index + 1
    pieces.append(text[start:].strip())
    return pieces


def fold(indent: str, statement: str) -> list[str]:
    """One C# statement or declaration, wrapped to the column limit."""
    if len(indent) + len(statement) <= WIDTH:
        return [indent + statement]
    head, assigned, tail = statement.partition(" = ")
    if assigned and "(" in tail:
        return [indent + head + " ="] + fold(indent + "    ", tail)
    for opener, closer in (("(", ")"), ("<", ">")):
        opening = statement.find(opener)
        closing = statement.rfind(closer)
        if opening < 0 or closing < opening:
            continue
        pieces = split_arguments(statement[opening + 1 : closing])
        pieces[-1] += statement[closing:]
        lines = [indent + statement[: opening + 1]]
        for index, piece in enumerate(pieces):
            trailing = "," if index + 1 < len(pieces) else ""
            lines.extend(fold(indent + "    ", piece + trailing))
        return lines
    return [indent + statement]


PASCAL_PART_OVERRIDES = {
    "AA": "AA",
    "AO": "AO",
    "FILENAME": "FileName",
    "FADEIN": "FadeIn",
    "FADEOUT": "FadeOut",
    "FX": "FX",
    "GI": "GI",
    "GZIP": "GZip",
    "HBOX": "HBox",
    "ID": "Id",
    "IO": "IO",
    "IP": "IP",
    "IV": "IV",
    "MACOS": "MacOS",
    "NODEPATH": "NodePath",
    "SPIRV": "SpirV",
    "STDIN": "StdIn",
    "STDOUT": "StdOut",
    "USERNAME": "UserName",
    "UV": "UV",
    "UV2": "UV2",
    "VBOX": "VBox",
    "WHITESPACE": "WhiteSpace",
    "WM": "WM",
    "XR": "XR",
    "XRAPI": "XRApi",
}


NUMBER = re.compile(r"^-?\d+(\.\d+)?$")

NUMERIC_DECLARED = {
    "sbyte",
    "byte",
    "short",
    "ushort",
    "int",
    "uint",
    "long",
    "ulong",
    "float",
    "double",
}

EMPTY_VALUES = {
    '&""': 'new StringName("")',
    "{}": "new Godot.Collections.Dictionary()",
    "[]": "new Godot.Collections.Array()",
    "PackedByteArray()": "System.Array.Empty<byte>()",
    "PackedStringArray()": "System.Array.Empty<string>()",
}


def default_value(declared: str, raw: str) -> tuple[str | None, str | None]:
    """One optional parameter as a C# default, and the statement its body owes.

    A C# default must be a compile time constant, and several of Godot's are
    not. Those take `null` and are filled in on entry, which is the same
    answer Godot's own generator reaches by declaring the parameter nullable.
    Answering None leaves the parameter required rather than guessing.
    """
    if raw == "null":
        return ("default", None) if declared == "Variant" else ("null", None)
    if raw in ("true", "false"):
        return raw, None
    if raw == "Callable()":
        return "default", None
    if raw == '""':
        return ('""', None) if declared == "string" else (None, None)
    if raw in EMPTY_VALUES:
        return "null", EMPTY_VALUES[raw]
    if NUMBER.match(raw):
        if declared in NUMERIC_DECLARED:
            return raw + ("f" if declared == "float" else ""), None
        value = "(%s)" % raw if raw.startswith("-") else raw
        return "(%s)%s" % (declared, value), None
    return None, None


def pascal(name: str, was_upper: bool = False) -> str:
    """Godot's own C# spelling for one snake case identifier.

    This follows `snake_to_pascal_case` in
    `modules/mono/utils/naming_utils.cpp` rather than capitalizing each part,
    because a C# caller reads `UserName` and `Mobile4G` in every other Godot
    binding and would read ours as the odd one out. The two rules a plain
    capitalization gets wrong are the part overrides above, which cannot be
    derived, and an uppercase letter after a digit.
    """
    parts = name.split("_")
    spelled: list[str] = []
    for index, part in enumerate(parts):
        override = PASCAL_PART_OVERRIDES.get(part.upper())
        if override:
            spelled.append(override)
            continue
        if part:
            held = list(part)
            held[0] = held[0].upper()
            for at in range(1, len(held)):
                if held[at - 1].isdigit():
                    held[at] = held[at].upper()
                elif was_upper:
                    held[at] = held[at].lower()
            spelled.append("".join(held))
        elif index == 0 or index == len(parts) - 1:
            spelled.append("_")
        else:
            spelled.append("__" if parts[index - 1] else "_")
    return "".join(spelled)


def camel(name: str) -> str:
    spelled = pascal(name).lstrip("_")
    spelled = spelled[:1].lower() + spelled[1:]
    return "@" + spelled if spelled in KEYWORDS else spelled


def member_names(entry: dict) -> set[str]:
    """Every C# name the members of one class take, which is what an enum
    sharing a property's name has to step around."""
    accessors: set[str] = set()
    for member in entry.get("properties", []):
        for role in ("getter", "setter"):
            if member.get(role):
                accessors.add(member[role])
    names = set(RESERVED) | {entry["name"]}
    names.update(pascal(member["name"]) for member in entry.get("properties", []))
    names.update(pascal(signal["name"]) for signal in entry.get("signals", []))
    names.update(
        pascal(method["name"])
        for method in entry.get("methods", [])
        if method["name"] not in accessors and not method.get("is_virtual")
    )
    return names


def shared_prefix(names: list[str]) -> int:
    """How many leading underscore-separated parts every constant in one enum
    repeats, which is the enum's own name said twice."""
    if not names:
        return 0
    parts = [name.split("_") for name in names]
    count = 0
    while all(count < len(one) - 1 for one in parts):
        if len({one[count] for one in parts}) != 1:
            break
        count += 1
    return count


def enum_constant(name: str, prefix: int) -> str:
    """One enum value, with the prefix its siblings share dropped. The name
    arrives upper case, which is the case Godot's own rule takes a flag for."""
    parts = name.split("_")
    while prefix > 0 and parts[prefix][:1].isdigit():
        prefix -= 1
    return pascal("_".join(part for part in parts[prefix:] if part), True)


def registered_classes(path: Path = REGISTRY) -> set[str]:
    """The classes this addon publishes, which is what tells them apart from
    the other GDExtensions a project has installed. A class registered only
    under NETW_TESTS is a test stand rather than a published one, and a class
    registered internally is reachable by name and by nothing else."""
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise fail("missing %s" % path)
    names: set[str] = set()
    guards: list[bool] = []
    for line in text.splitlines():
        opening = line.strip()
        if opening.startswith("#if"):
            guards.append(TEST_GUARD in opening)
        elif opening.startswith("#elif"):
            if guards:
                guards[-1] = TEST_GUARD in opening
        elif opening.startswith("#else"):
            if guards:
                guards[-1] = False
        elif opening.startswith("#endif"):
            if guards:
                guards.pop()
        elif not any(guards):
            names.update(REGISTRATION.findall(line))
    if not names:
        raise fail("%s carries no GDREGISTER_CLASS lines" % path)
    return names


class Surface:
    """The extension's own classes, indexed by name."""

    def __init__(self, dump: dict, owned: set[str] | None = None):
        published = {entry["name"]: entry for entry in dump.get("classes", []) if entry.get("api_type") == "extension"}
        if not published:
            raise fail(
                "the dump carries no api_type 'extension' classes, so the addon " "was not loaded when it was written"
            )
        self.classes = (
            published if owned is None else {name: entry for name, entry in published.items() if name in owned}
        )
        if not self.classes:
            raise fail(
                "the dump carries none of the classes %s registers, so it was "
                "written against a project without this addon" % REGISTRY.name
            )
        self.engine = {entry["name"] for entry in dump.get("classes", []) if entry.get("api_type") != "extension"}
        self.subclassed = {
            entry.get("inherits") for entry in self.classes.values() if entry.get("inherits") in self.classes
        }
        self.enums: dict[str, str] = {}
        for entry in dump.get("classes", []):
            taken = member_names(entry)
            for held in entry.get("enums", []):
                spelled = pascal(held["name"])
                if spelled in taken:
                    spelled += "Enum"
                self.enums["%s.%s" % (entry["name"], held["name"])] = spelled

    def is_object(self, type_name: str) -> bool:
        return type_name in self.classes or type_name in self.engine

    def engine_name(self, type_name: str) -> str:
        return ENGINE_NAMES.get(type_name, type_name)

    def enum_name(self, type_name: str) -> str | None:
        """The C# type for an enum:: or bitfield:: spelling, or None when it
        has no name a caller can reach and crosses as a plain integer."""
        spelled = type_name.split("::", 1)[1]
        owner, dot, leaf = spelled.partition(".")
        if not dot:
            return spelled if spelled in GLOBAL_ENUMS else None
        if owner == "Variant":
            return "Variant.%s" % leaf
        held = self.enums.get(spelled)
        if held is None:
            return None
        return "%s.%s" % (self.engine_name(owner), held)

    def signal_type(self, argument: dict) -> str | None:
        """The delegate parameter type for one signal argument. A handle this
        addon owns arrives as a Variant, because GodotSharp can marshal only
        the types its own assembly knows."""
        type_name = argument.get("type", "")
        if type_name in ("void", "Object") or type_name in self.classes:
            return "Variant"
        boxed = self.box(argument)
        return None if boxed is None else boxed.declared

    def encode(self, entry: dict | None, role: str = "argument") -> Encoded | None:
        """The ptrcall encoding for a return value or argument, or None when
        the type cannot cross a ptrcall unaided."""
        if entry is None:
            return Encoded("void", "void", "", "")
        type_name = entry.get("type", "")
        if type_name.startswith("enum::") or type_name.startswith("bitfield::"):
            declared = self.enum_name(type_name)
            if declared is None:
                return INTEGER_SLOTS["int64"]
            return Encoded(declared, "long", "(long){name}", "(%s){name}" % declared)
        meta = entry.get("meta")
        if type_name == "int" and meta in INTEGER_SLOTS:
            return INTEGER_SLOTS[meta]
        if type_name == "float" and meta == "float":
            return Encoded("float", "float", "{name}", "{name}")
        if type_name in BLITTABLE:
            return BLITTABLE[type_name]
        if type_name in self.classes:
            if role == "return":
                return Encoded(
                    type_name,
                    "IntPtr",
                    "",
                    "%s.Adopt({name})" % type_name,
                )
            return Encoded(
                type_name,
                "IntPtr",
                "{name}?.Native ?? IntPtr.Zero",
                "{name}",
            )
        if type_name in self.engine:
            if role == "return":
                return None
            return Encoded(
                self.engine_name(type_name),
                "IntPtr",
                "{name}?.NativeInstance ?? IntPtr.Zero",
                "{name}",
            )
        return None

    def box(self, entry: dict | None) -> Boxed | None:
        """The Variant encoding for a return value or argument. Every type the
        dump names has one, so a None answer is a gap in the tables above."""
        if entry is None:
            return Boxed("void", "", "")
        type_name = entry.get("type", "")
        if type_name.startswith("enum::") or type_name.startswith("bitfield::"):
            declared = self.enum_name(type_name)
            if declared is None:
                return INTEGER_BOXES["int64"]
            return Boxed(
                declared,
                "VariantUtils.CreateFromInt((long){name})",
                "(%s)VariantUtils.ConvertToInt64({name})" % declared,
            )
        meta = entry.get("meta")
        if type_name == "int":
            return INTEGER_BOXES.get(meta, INTEGER_BOXES["int64"])
        if type_name == "float" and meta == "float":
            return Boxed(
                "float",
                "VariantUtils.CreateFromFloat({name})",
                "VariantUtils.ConvertToFloat32({name})",
            )
        if type_name.startswith("typedarray::"):
            return BUILTIN_BOXES["Array"]
        if type_name in BUILTIN_BOXES:
            return BUILTIN_BOXES[type_name]
        if type_name in self.classes:
            read = "VariantUtils.ConvertToGodotObjectPtr({name})"
            if self.classes[type_name].get("is_refcounted"):
                read = "NetwApi.Retained(%s)" % read
            return Boxed(
                type_name,
                "VariantUtils.CreateFromGodotObjectPtr(" "{name}?.Native ?? IntPtr.Zero)",
                "%s.Adopt(%s)" % (type_name, read),
            )
        if type_name in self.engine:
            declared = self.engine_name(type_name)
            return Boxed(
                declared,
                "VariantUtils.CreateFromGodotObject({name})",
                "(%s)VariantUtils.ConvertToGodotObject({name})" % declared,
            )
        return None


class Emitter:
    """Writes one class file from its dump entry."""

    def __init__(self, surface: Surface):
        self.surface = surface
        self.skipped: list[str] = []
        self.taken: set[str] = set()
        self.binds: dict[str, str] = {}
        self.thunks = Thunks()
        self.exports: dict[str, dict[str, str]] = {}
        self.required: list[str] = []
        self.prose: csharp_docs.Prose | None = None
        self.docs: dict[str, csharp_docs.Docs] = {}

    def emit(self, entry: dict) -> str:
        name = entry["name"]
        self.taken = set(RESERVED) | {name}
        self.binds = {}
        held = self.docs.get(name) if self.prose is not None else None
        lines = [
            "// <auto-generated/>",
            "using System;",
            "using Godot;",
            "using Godot.NativeInterop;",
            "",
            "namespace Networked;",
            "",
            *(
                csharp_docs.block(
                    "",
                    self.prose.render(name, held.brief),
                    self.prose.render(name, held.description),
                )
                if held is not None
                else []
            ),
            "public %sclass %s : %s"
            % (
                "" if name in self.surface.subclassed else "sealed ",
                name,
                self.base_of(entry),
            ),
            "{",
            *self.lifetime_of(entry),
        ]
        methods = {method["name"]: method for method in entry.get("methods", [])}
        accessors = self.accessor_names(entry)

        for held in entry.get("enums", []):
            emitted = self.enum_of(name, held)
            if emitted:
                lines.extend(["", *emitted])

        held = self.constants_of(name, entry)
        if held:
            lines.extend(["", *held])

        for signal in entry.get("signals", []):
            emitted = self.signal_of(name, signal)
            if emitted:
                lines.extend(["", *emitted])

        for member in entry.get("properties", []):
            emitted = self.property_of(name, member, methods)
            if emitted:
                lines.extend(["", *emitted])

        for method in entry.get("methods", []):
            if method["name"] in accessors or method.get("is_virtual"):
                continue
            emitted = self.method_of(name, method)
            if emitted:
                lines.extend(["", *emitted])

        lines.append("}")
        wrapped: list[str] = []
        for line in lines:
            statement = line.lstrip()
            if statement.startswith("///"):
                wrapped.append(line)
            elif statement:
                wrapped.extend(fold(line[: len(line) - len(statement)], statement))
            else:
                wrapped.append(line)
        return "\n".join(wrapped) + "\n"

    def base_of(self, entry: dict) -> str:
        inherits = entry.get("inherits")
        if inherits in self.surface.classes:
            return inherits
        return "NetwRefCounted" if entry.get("is_refcounted") else "NetwObject"

    def lifetime_of(self, entry: dict) -> list[str]:
        name = entry["name"]
        inherited = entry.get("inherits") in self.surface.classes
        if inherited or entry.get("is_refcounted"):
            opening = "    public %s(IntPtr native) : base(native)" % name
        else:
            opening = "    public %s(IntPtr native) : base(native, owned: false)" % name
        return [
            opening,
            "    {",
            "    }",
            "",
            "    public %sstatic %s Adopt(IntPtr native)" % ("new " if inherited else "", name),
            "    {",
            "        return native == IntPtr.Zero ? null : new %s(native);" % name,
            "    }",
            "",
            "    public %sstatic %s From(Variant value)" % ("new " if inherited else "", name),
            "    {",
            "        return Adopt(%s);"
            % (
                "NetwApi.Retained(NetwApi.ObjectOf(value))" if entry.get("is_refcounted") else "NetwApi.ObjectOf(value)"
            ),
            "    }",
        ]

    def enum_of(self, class_name: str, held: dict) -> list[str] | None:
        subject = "%s.%s" % (class_name, held["name"])
        spelled = self.surface.enums[subject]
        if not self.claim(subject, spelled):
            return None
        values = held.get("values", [])
        prefix = shared_prefix([one["name"] for one in values])
        lines = []
        if held.get("is_bitfield"):
            lines.append("    [Flags]")
        lines.extend(["    public enum %s : long" % spelled, "    {"])
        taken: set[str] = set()
        for one in values:
            name = enum_constant(one["name"], prefix)
            while name in taken:
                name += "_"
            taken.add(name)
            self.record("%s.%s" % (class_name, one["name"]), "%s.%s" % (spelled, name))
            lines.extend(self.doc_of(class_name, "constants", one["name"], "        "))
            lines.append("        %s = %d," % (name, one["value"]))
        lines.append("    }")
        return lines

    def constants_of(self, class_name: str, entry: dict) -> list[str]:
        lines = []
        for one in entry.get("constants", []):
            spelled = enum_constant(one["name"], 0)
            if not self.claim("%s.%s" % (class_name, one["name"]), spelled):
                continue
            lines.extend(self.doc_of(class_name, "constants", one["name"], "    "))
            lines.append("    public const long %s = %d;" % (spelled, one["value"]))
        return lines

    def signal_of(self, class_name: str, signal: dict) -> list[str] | None:
        subject = "%s.%s" % (class_name, signal["name"])
        spelled = pascal(signal["name"])
        carried = []
        for argument in signal.get("arguments") or []:
            declared = self.surface.signal_type(argument)
            if declared is None:
                self.skipped.append(subject)
                return None
            carried.append(declared)
        if not self.claim(subject, spelled):
            return None
        handler = "Action" if not carried else "Action<%s>" % ", ".join(carried)
        return [
            *self.doc_of(class_name, "signals", signal["name"], "    "),
            "    public event %s %s" % (handler, spelled),
            "    {",
            '        add => Connect("%s", Callable.From(value));' % signal["name"],
            '        remove => Disconnect("%s", Callable.From(value));' % signal["name"],
            "    }",
        ]

    def accessor_names(self, entry: dict) -> set[str]:
        names: set[str] = set()
        for member in entry.get("properties", []):
            for role in ("getter", "setter"):
                if member.get(role):
                    names.add(member[role])
        return names

    def claim(self, subject: str, spelled: str) -> bool:
        if spelled in self.taken:
            self.skipped.append(subject)
            return False
        self.taken.add(spelled)
        self.record(subject, spelled)
        return True

    def record(self, subject: str, spelled: str) -> None:
        """What one published name is called in C#, so a documentation
        reference resolves through the emitter rather than by guessing."""
        owner, _, leaf = subject.rpartition(".")
        self.exports.setdefault(owner, {})[leaf] = spelled

    def doc_of(
        self,
        class_name: str,
        kind: str,
        name: str,
        indent: str,
        params: dict | None = None,
    ) -> list[str]:
        """The `///` comment for one member, or nothing on the pass that has
        no prose loaded yet."""
        if self.prose is None:
            return []
        held = self.docs.get(class_name)
        if held is None:
            return []
        text = getattr(held, kind).get(name, "")
        return csharp_docs.block(indent, self.prose.render(class_name, text, params))

    def bind_field(self, class_name: str, method: dict) -> tuple[str, list[str]]:
        """The field holding this method's bind, declared once however many
        members call through it."""
        name = method["name"]
        if name in self.binds:
            return self.binds[name], []
        field = "_bind" + pascal(name).lstrip("_")
        while field in self.binds.values():
            field += "_"
        self.binds[name] = field
        line = "    private static readonly IntPtr %s = " 'NetwApi.MethodBind("%s", "%s", %dUL);' % (
            field,
            class_name,
            name,
            method["hash"],
        )
        return field, [line]

    def declared_arguments(self, arguments: list[dict], names: list[str], plans: list) -> tuple[str, list[str]]:
        """The parameter list one method publishes, and the statements its
        body owes for a default C# cannot spell as a constant."""
        spelled: list[str] = []
        filled: list[str] = []
        for argument, name, one in zip(arguments, names, plans):
            text = "%s %s" % (one.declared, name)
            raw = argument.get("default_value")
            if raw is not None:
                value, substitute = default_value(one.declared, raw)
                if value is None:
                    self.required.append("%s %s = %s" % (one.declared, name, raw))
                else:
                    text += " = %s" % value
                    if substitute is not None:
                        filled.append("        %s ??= %s;" % (name, substitute))
            spelled.append(text)
        return ", ".join(spelled), filled

    def instance_of(self, method: dict) -> str:
        return "IntPtr.Zero" if method.get("is_static") else "Checked"

    def parameters(self, method: dict) -> list[str]:
        spelled: list[str] = []
        for index, argument in enumerate(method.get("arguments") or []):
            name = camel(argument["name"])
            if name in spelled or name.lstrip("@") == VARARG_PARAMETER:
                name = "%s%d" % (name, index)
            spelled.append(name)
        return spelled

    def ptrcall_lines(
        self,
        field: str,
        instance: str,
        arguments: list[tuple[str, Encoded]],
        answered: Encoded,
        indent: str,
    ) -> list[str]:
        body = []
        passed = []
        for index, (expression, encoded) in enumerate(arguments):
            slot = "slot%d" % index
            body.append(indent + "%s %s = %s;" % (encoded.slot, slot, encoded.to_slot.format(name=expression)))
            passed.append("in %s" % slot)

        thunk = self.thunks.ptrcall([encoded.slot for _, encoded in arguments], answered.slot)
        held = returned_slot(answered.slot)
        answer = "answered" if answered.declared != "void" else "discarded"
        body.append(indent + "%s %s = default;" % (held, answer))
        body.append(
            indent
            + "NetwThunks.%s(%s);"
            % (
                thunk,
                ", ".join([field, instance, *passed, "ref %s" % answer]),
            )
        )
        if answered.declared != "void":
            body.append(indent + "return %s;" % answered.from_slot.format(name="answered"))
        return body

    def call_lines(
        self,
        field: str,
        instance: str,
        arguments: list[tuple[str, Boxed]],
        answered: Boxed,
        indent: str,
        varargs: str | None = None,
    ) -> list[str]:
        fixed = len(arguments)
        if varargs is not None or fixed > CALL_MAX_ARITY:
            return self.pack_lines(field, instance, arguments, answered, indent, varargs)

        body = []
        passed = []
        for index, (expression, boxed) in enumerate(arguments):
            slot = "slot%d" % index
            body.append(indent + "godot_variant %s = %s;" % (slot, boxed.to_variant.format(name=expression)))
            passed.append("in %s" % slot)

        body.append(indent + "godot_variant answered = default;")
        body.append(
            indent
            + "NetwThunks.%s(%s);"
            % (
                self.thunks.call(fixed),
                ", ".join([field, instance, *passed, "ref answered"]),
            )
        )
        for index in range(fixed):
            body.append(indent + "slot%d.Dispose();" % index)
        return body + self.answer_lines(answered, indent)

    def pack_lines(
        self,
        field: str,
        instance: str,
        arguments: list[tuple[str, Boxed]],
        answered: Boxed,
        indent: str,
        varargs: str | None,
    ) -> list[str]:
        """The unbounded tail, and any arity past the fixed thunks, carried in
        a pack the extension owns. It costs one allocation, which is why every
        shorter call takes a thunk instead."""
        self.thunks.pack()
        fixed = len(arguments)
        body = []
        if varargs is not None:
            body.append(indent + "int total = %d + (%s == null ? 0 : %s.Length);" % (fixed, varargs, varargs))
            total = "total"
        else:
            total = str(fixed)
        body.append(indent + "IntPtr pack = NetwThunks.ArgsNew(%s);" % total)
        for index, (expression, boxed) in enumerate(arguments):
            body.extend(
                [
                    indent + "godot_variant slot%d = %s;" % (index, boxed.to_variant.format(name=expression)),
                    indent + "NetwThunks.ArgsSet(pack, %d, in slot%d);" % (index, index),
                    indent + "slot%d.Dispose();" % index,
                ]
            )
        if varargs is not None:
            body.extend(
                [
                    indent + "for (int index = %d; index < total; index++)" % fixed,
                    indent + "{",
                    indent + "    godot_variant carried = %s[index - %d]" ".CopyNativeVariant();" % (varargs, fixed),
                    indent + "    NetwThunks.ArgsSet(pack, index, in carried);",
                    indent + "    carried.Dispose();",
                    indent + "}",
                ]
            )
        body.extend(
            [
                indent + "godot_variant answered = default;",
                indent + "NetwThunks.CallPack(%s, %s, pack, %s, ref answered);" % (field, instance, total),
                indent + "NetwThunks.ArgsFree(pack);",
            ]
        )
        return body + self.answer_lines(answered, indent)

    def answer_lines(self, answered: Boxed, indent: str) -> list[str]:
        if answered.declared == "void":
            return [indent + "answered.Dispose();"]
        return [
            indent + "%s result = %s;" % (answered.declared, answered.from_variant.format(name="answered")),
            indent + "answered.Dispose();",
            indent + "return result;",
        ]

    def ptrcall_plan(self, method: dict, arguments: list[dict]) -> tuple[Encoded, list[Encoded]] | None:
        if method.get("is_vararg") or len(arguments) > PTRCALL_MAX_ARITY:
            return None
        answered = self.surface.encode(method.get("return_value"), "return")
        if answered is None:
            return None
        encoded = []
        for argument in arguments:
            one = self.surface.encode(argument)
            if one is None:
                return None
            encoded.append(one)
        return answered, encoded

    def call_plan(self, method: dict, arguments: list[dict]) -> tuple[Boxed, list[Boxed]] | None:
        answered = self.surface.box(method.get("return_value"))
        if answered is None:
            return None
        boxed = []
        for argument in arguments:
            one = self.surface.box(argument)
            if one is None:
                return None
            boxed.append(one)
        return answered, boxed

    def method_of(self, class_name: str, method: dict) -> list[str] | None:
        subject = "%s.%s" % (class_name, method["name"])
        spelled = pascal(method["name"])
        if not self.claim(subject, spelled):
            return None
        arguments = list(method.get("arguments") or [])
        names = self.parameters(method)

        field, declared = self.bind_field(class_name, method)
        head = [*declared, ""] if declared else []
        instance = self.instance_of(method)
        modifier = "public static " if method.get("is_static") else "public "

        told = self.doc_of(
            class_name,
            "methods",
            method["name"],
            "    ",
            {argument["name"]: name for argument, name in zip(arguments, names)},
        )

        plan = self.ptrcall_plan(method, arguments)
        if plan is not None:
            answered, encoded = plan
            signature, filled = self.declared_arguments(arguments, names, encoded)
            body = filled + self.ptrcall_lines(field, instance, list(zip(names, encoded)), answered, "        ")
            return [
                *head,
                *told,
                "    %s%s %s(%s)" % (modifier, answered.declared, spelled, signature),
                "    {",
                *body,
                "    }",
            ]

        plan = self.call_plan(method, arguments)
        if plan is None:
            self.skipped.append(subject)
            return None
        answered, boxed = plan
        signature, filled = self.declared_arguments(arguments, names, boxed)
        varargs = None
        if method.get("is_vararg"):
            varargs = VARARG_PARAMETER
            tail = "params Variant[] %s" % VARARG_PARAMETER
            signature = "%s, %s" % (signature, tail) if signature else tail
        body = filled + self.call_lines(
            field,
            instance,
            list(zip(names, boxed)),
            answered,
            "        ",
            varargs,
        )
        return [
            *head,
            *told,
            "    %s%s %s(%s)" % (modifier, answered.declared, spelled, signature),
            "    {",
            *body,
            "    }",
        ]

    def property_of(self, class_name: str, member: dict, methods: dict) -> list[str] | None:
        subject = "%s.%s" % (class_name, member["name"])
        spelled = pascal(member["name"])
        getter = methods.get(member.get("getter", ""))
        if getter is None:
            self.skipped.append(subject)
            return None
        index = member.get("index")
        held = [] if index is None else [str(index)]
        if len(getter.get("arguments") or []) != len(held):
            self.skipped.append(subject)
            return None
        if not self.claim(subject, spelled):
            return None

        read = self.accessor_of(class_name, getter, held, None)
        if read is None:
            self.skipped.append(subject)
            return None
        declared, reader, declaration = read

        binds = [declaration]
        writer: list[str] = []
        setter = methods.get(member.get("setter", ""))
        if setter is not None:
            written = self.accessor_of(class_name, setter, held, "value")
            if written is not None and written[0] == declared:
                writer = written[1]
                binds.append(written[2])

        lines: list[str] = []
        for declared_lines in binds:
            if declared_lines:
                if lines:
                    lines.append("")
                lines.extend(declared_lines)
        if lines:
            lines.append("")
        for role in ("getter", "setter"):
            if member.get(role):
                self.record("%s.%s" % (class_name, member[role]), spelled)
        lines.extend(
            [
                *self.doc_of(class_name, "members", member["name"], "    "),
                "    public %s %s" % (declared, spelled),
                "    {",
                "        get",
                "        {",
                *reader,
                "        }",
            ]
        )
        if writer:
            lines.extend(["        set", "        {", *writer, "        }"])
        lines.append("    }")
        return lines

    def accessor_of(
        self, class_name: str, method: dict, held: list[str], value: str | None
    ) -> tuple[str, list[str], list[str]] | None:
        """The declared type, body and bind declaration for one accessor."""
        arguments = list(method.get("arguments") or [])
        expressions = list(held)
        if value is not None:
            expressions.append(value)
        if len(arguments) != len(expressions):
            return None

        field, declaration = self.bind_field(class_name, method)
        instance = self.instance_of(method)
        indent = "            "

        plan = self.ptrcall_plan(method, arguments)
        if plan is not None:
            answered, encoded = plan
            if value is not None:
                declared = encoded[-1].declared
                answered = Encoded("void", "void", "", "")
            else:
                declared = answered.declared
            body = self.ptrcall_lines(field, instance, list(zip(expressions, encoded)), answered, indent)
            return declared, body, declaration

        plan = self.call_plan(method, arguments)
        if plan is None:
            return None
        answered, boxed = plan
        if value is not None:
            declared = boxed[-1].declared
            answered = Boxed("void", "", "")
        else:
            declared = answered.declared
        body = self.call_lines(field, instance, list(zip(expressions, boxed)), answered, indent)
        return declared, body, declaration


def pass_through(parameter: str) -> str:
    """One declared parameter as the argument that forwards it, keeping the
    `in` or `ref` the address depends on."""
    pieces = parameter.split(" ")
    if pieces[0] in ("in", "ref", "out"):
        return "%s %s" % (pieces[0], pieces[-1])
    return pieces[-1]


THUNKS_HEAD = """// <auto-generated/>
using System;
using System.Runtime.InteropServices;
using Godot;
using Godot.NativeInterop;

namespace Networked;

/// <summary>
/// The fixed-arity entry points every generated body calls through.
/// </summary>
/// <remarks>
/// The addon exports one native function per arity rather than one variadic
/// one, so each argument is its own parameter and the CLR passes its address.
/// That is what lets these bindings compile in a project that has not set
/// <c>AllowUnsafeBlocks</c>.
/// </remarks>
internal static class NetwThunks
{"""


def thunk_registry(thunks: Thunks) -> str:
    """The delegate types and the lazily bound instances behind them."""
    lines: list[str] = []

    def declare(name: str, parameters: list[str], exported: str) -> None:
        field = "_%s%s" % (name[:1].lower(), name[1:])
        signature = ", ".join(parameters)
        handed = ", ".join(pass_through(one) for one in parameters)
        lines.extend(
            [
                "",
                "    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]",
                "    private delegate void %sFn(%s);" % (name, signature),
                "",
                "    private static %sFn %s;" % (name, field),
                "",
                "    internal static void %s(%s)" % (name, signature),
                "    {",
                '        %sFn thunk = %s ??= NetwApi.Thunk<%sFn>("%s");' % (name, field, name, exported),
                "        thunk(%s);" % handed,
                "    }",
            ]
        )

    for slots, answered in sorted(thunks.ptrcalls):
        declare(
            Thunks.ptrcall_name(slots, answered),
            [
                "IntPtr bind",
                "IntPtr instance",
                *("in %s a%d" % (slot, index) for index, slot in enumerate(slots)),
                "ref %s answered" % answered,
            ],
            "ptrcall%d" % len(slots),
        )

    for arity in sorted(thunks.calls):
        declare(
            "Call%d" % arity,
            [
                "IntPtr bind",
                "IntPtr instance",
                *("in godot_variant a%d" % index for index in range(arity)),
                "ref godot_variant answered",
            ],
            "call%d" % arity,
        )

    if thunks.packed:
        lines.extend(
            [
                "",
                "    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]",
                "    private delegate IntPtr ArgsNewFn(long count);",
                "",
                "    private static ArgsNewFn _argsNew;",
                "",
                "    internal static IntPtr ArgsNew(long count)",
                "    {",
                "        ArgsNewFn thunk = _argsNew ??= " 'NetwApi.Thunk<ArgsNewFn>("args_new");',
                "        return thunk(count);",
                "    }",
            ]
        )
        declare(
            "ArgsSet",
            ["IntPtr pack", "long index", "in godot_variant value"],
            "args_set",
        )
        declare(
            "CallPack",
            [
                "IntPtr bind",
                "IntPtr instance",
                "IntPtr pack",
                "long count",
                "ref godot_variant answered",
            ],
            "call_pack",
        )
        declare("ArgsFree", ["IntPtr pack"], "args_free")

    wrapped = [THUNKS_HEAD]
    for line in lines[1:]:
        statement = line.lstrip()
        if statement:
            wrapped.extend(fold(line[: len(line) - len(statement)], statement))
        else:
            wrapped.append(line)
    wrapped.append("}")
    return "\n".join(wrapped) + "\n"


def dump_api(godot: str, destination: Path, timeout: float) -> Path:
    """Write the API dump from a project that has the addon installed."""
    run([godot, "--headless", "--path", str(ROOT), "--import"], timeout=timeout, check=False)
    run(
        [godot, "--headless", "--path", str(ROOT), "--dump-extension-api"],
        cwd=ROOT,
        timeout=timeout,
    )
    written = ROOT / DUMP_NAME
    if not written.is_file():
        raise fail("the engine wrote no %s" % DUMP_NAME)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(written), str(destination))
    return destination


def write_engine_names(dump_path: Path) -> dict:
    """Rewrite the committed table from GodotSharp's own documentation.

    Run this when the engine's minor version moves, or when a documentation
    reference names an engine member the table has never seen. Everything
    else in the pipeline reads the table.
    """
    surface = Surface(load_json(dump_path), registered_classes())
    sharp = csharp_docs.SharpDocs.load(sharp_docs(dump_path))
    docs = csharp_docs.Docs.load(DOC_DIR)
    builtins = {name: spelled for name, spelled in BUILTIN_CREFS.items() if "[" not in spelled}

    names: dict[str, str] = {}
    missing: list[str] = []
    for owner, held in sorted(docs.items()):
        bodies = [held.brief, held.description]
        bodies += list(held.methods.values()) + list(held.members.values())
        bodies += list(held.signals.values()) + list(held.constants.values())
        for body in bodies:
            for kind, target in csharp_docs.REFERENCE.findall(body):
                if kind == "param" or "." not in target:
                    continue
                outer = target.split(".")[0]
                leaf = target.split(".")[-1]
                if outer in surface.classes or leaf.startswith("_"):
                    continue
                spelled = surface.engine_name(outer) if outer in surface.engine else builtins.get(outer)
                if spelled is None:
                    continue
                key = "%s %s.%s" % (kind, spelled, leaf)
                if key in names:
                    continue
                found = sharp.engine_member(spelled, kind, leaf)
                if found is None:
                    missing.append("[%s %s]" % (kind, target))
                else:
                    names[key] = found

    NAMES_FILE.write_text(json.dumps(names, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return {"written": len(names), "missing": sorted(set(missing))}


def emit_tree(dump_path: Path, output: Path, only: list[str] | None) -> dict:
    surface = Surface(load_json(dump_path), registered_classes())
    emitter = Emitter(surface)
    chosen = sorted(only or surface.classes)
    unknown = [name for name in chosen if name not in surface.classes]
    if unknown:
        raise fail("the dump carries no class named %s" % ", ".join(unknown))

    output.mkdir(parents=True, exist_ok=True)
    for stale in output.glob("*.cs"):
        if stale.name not in HAND_WRITTEN:
            stale.unlink()

    for name in sorted(surface.classes):
        emitter.emit(surface.classes[name])
    emitter.docs = csharp_docs.Docs.load(DOC_DIR)
    emitter.prose = csharp_docs.Prose(
        emitter.exports,
        set(surface.classes),
        {one: surface.engine_name(one) for one in surface.engine},
        BUILTIN_CREFS,
        engine_names(),
    )
    emitted = {name: emitter.emit(surface.classes[name]) for name in sorted(surface.classes)}
    written = []
    for name in chosen:
        (output / ("%s.cs" % name)).write_text(emitted[name], encoding="utf-8")
        written.append(name)
    (output / THUNKS_NAME).write_text(thunk_registry(emitter.thunks), encoding="utf-8")

    return {
        "written": written,
        "skipped": sorted(set(emitter.skipped)),
        "fallbacks": emitter.prose.fallbacks,
        "required": sorted(set(emitter.required)),
        "blocks": emitter.prose.blocks_carried,
        "twins_dropped": emitter.prose.gdscript_twins_dropped,
    }


def check_tree(dump_path: Path, output: Path) -> dict:
    """Refuse when the committed tree is not what the dump would write."""
    with tempfile.TemporaryDirectory() as workspace:
        fresh = Path(workspace) / "cs"
        report = emit_tree(dump_path, fresh, None)
        drifted = []
        for written in sorted(fresh.glob("*.cs")):
            held = output / written.name
            if not held.is_file():
                drifted.append("%s is missing" % written.name)
            elif held.read_text(encoding="utf-8") != written.read_text(encoding="utf-8"):
                drifted.append("%s is stale" % written.name)
        emitted = {written.name for written in fresh.glob("*.cs")}
        for held in sorted(output.glob("*.cs")):
            if held.name not in emitted and held.name not in HAND_WRITTEN:
                drifted.append("%s is no longer generated" % held.name)
    if drifted:
        raise fail(
            "the committed bindings are not what the dump writes, so run "
            "`ci/csharp.py emit`. %s" % ", ".join(drifted)
        )

    pointered = []
    for held in sorted(output.glob("*.cs")):
        text = held.read_text(encoding="utf-8")
        for spelling in ("unsafe", "stackalloc"):
            if spelling in text:
                pointered.append("%s carries `%s`" % (held.name, spelling))
    if pointered:
        raise fail(
            "a consumer compiles these files without AllowUnsafeBlocks, so no "
            "shipped binding may carry a pointer. %s" % ", ".join(pointered)
        )
    return report


PROJECT = """<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>disable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <GenerateDocumentationFile>true</GenerateDocumentationFile>
    <NoWarn>1591;1573;419</NoWarn>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="%s/*.cs" />
    <PackageReference Include="GodotSharp" Version="%s" />
  </ItemGroup>
</Project>
"""


def sharp_version(dump_path: Path) -> str:
    """The GodotSharp package a dump's engine agrees with. A patch release
    does not move the C# API, so the minor's first patch is the copy."""
    header = load_json(dump_path).get("header", {})
    return "%d.%d.0" % (
        int(header.get("version_major", 4)),
        int(header.get("version_minor", 0)),
    )


def sharp_docs(dump_path: Path) -> Path:
    """GodotSharp's own documentation file, in the NuGet cache.

    Only `ci/csharp.py names` reads this. `emit` reads the committed table it
    produces instead, so generating the bindings needs the repository and the
    dump and nothing else.
    """
    found = (
        Path.home()
        / ".nuget"
        / "packages"
        / "godotsharp"
        / sharp_version(dump_path)
        / "lib"
        / "net8.0"
        / "GodotSharp.xml"
    )
    if not found.is_file():
        raise fail(
            "no GodotSharp documentation at %s. Run `ci/csharp.py compile` " "first, which restores the package" % found
        )
    return found


def engine_names() -> dict:
    """The C# spelling of every engine name this tree's documentation cites.

    This is committed, because it is the one input that cannot be derived
    from the repository. It changes only when the engine's minor does, which
    is far less often than the dump, and reading it from a file rather than
    from the NuGet cache is what keeps `emit` reproducible and keeps `check`
    from depending on a package `compile` has not restored yet.
    """
    try:
        return load_json(NAMES_FILE)
    except FileNotFoundError:
        raise fail(
            "missing %s, so an engine reference cannot be resolved. Run "
            "`ci/csharp.py names` to write it" % NAMES_FILE
        )


def sharp_range(dump_path: Path) -> str:
    """The GodotSharp versions this dump's engine publishes, as a NuGet range
    over the minor, because a patch release does not move the C# API."""
    header = load_json(dump_path).get("header", {})
    major = int(header.get("version_major", 4))
    minor = int(header.get("version_minor", 0))
    return "[%d.%d.0,%d.%d.0)" % (major, minor, major, minor + 1)


def compile_tree(output: Path, workspace: Path, version: str, timeout: float) -> int:
    """Compile the emitted tree against GodotSharp, which is the only reader
    that catches a name or an encoding the generator got wrong."""
    if workspace.exists():
        shutil.rmtree(workspace)
    workspace.mkdir(parents=True)
    project = workspace / "bindings.csproj"
    project.write_text(PROJECT % (output, version), encoding="utf-8")
    run(
        ["dotnet", "build", str(project), "--nologo", "-v", "quiet"],
        cwd=workspace,
        timeout=timeout,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", default="godot")
    parser.add_argument("--timeout", type=float, default=900.0)
    parser.add_argument("--dump", type=Path, default=CI_DIR / "cache" / DUMP_NAME)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("dump")
    sub.add_parser("names")
    emit = sub.add_parser("emit")
    emit.add_argument("--only", action="append")
    emit.add_argument("--output", type=Path, default=OUTPUT_DIR)

    check = sub.add_parser("check")
    check.add_argument("--output", type=Path, default=OUTPUT_DIR)

    compile_check = sub.add_parser("compile")
    compile_check.add_argument("--output", type=Path, default=OUTPUT_DIR)
    compile_check.add_argument("--workspace", type=Path, default=CI_DIR / "cache" / "bindings")
    compile_check.add_argument("--sharp-version")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.action == "dump":
        written = dump_api(args.godot, args.dump, args.timeout)
        print("DUMP %s" % written)
        return 0

    if not args.dump.is_file():
        raise fail("no dump at %s, run `ci/csharp.py dump` first" % args.dump)

    if args.action == "names":
        report = write_engine_names(args.dump)
        print("NAMES engine=%d unresolved=%d" % (report["written"], len(report["missing"])))
        for one in report["missing"][:20]:
            print("  no C# name for %s" % one)
        return 0

    if args.action == "compile":
        version = args.sharp_version or sharp_range(args.dump)
        print("COMPILE GodotSharp %s" % version)
        return compile_tree(args.output, args.workspace, version, args.timeout)

    if args.action == "check":
        report = check_tree(args.dump, args.output)
        print("CHECK classes=%d skipped=%d" % (len(report["written"]), len(report["skipped"])))
        return 0

    report = emit_tree(args.dump, args.output, args.only)
    print(
        "EMIT classes=%d skipped=%d docs_plain=%d blocks=%d twins_dropped=%d"
        " defaults_refused=%d"
        % (
            len(report["written"]),
            len(report["skipped"]),
            len(report["fallbacks"]),
            report["blocks"],
            report["twins_dropped"],
            len(report["required"]),
        )
    )
    for name in report["skipped"][:20]:
        print("  skipped %s" % name)
    for one in report["required"][:20]:
        print("  no C# default for %s" % one)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
