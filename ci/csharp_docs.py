#!/usr/bin/env python3
"""Carry the published class documentation into the generated C# bindings.

The engine's API dump has no prose in it at all, so the descriptions a C#
caller hovers come from `extension/doc_classes/*.xml`, the same tree the
in-editor help serves. This module reads that tree and rewrites Godot's
BBCode as C# documentation XML.
"""

from __future__ import annotations

import re
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path

WIDTH = 80


class Docs:
    """One class's published documentation, indexed by member name."""

    def __init__(self, path: Path):
        root = ET.parse(path).getroot()
        self.brief = (root.findtext("brief_description") or "").strip()
        self.description = (root.findtext("description") or "").strip()
        self.methods: dict[str, str] = {}
        self.members: dict[str, str] = {}
        self.signals: dict[str, str] = {}
        self.constants: dict[str, str] = {}

        for found in root.findall("./methods/method"):
            self.methods[found.get("name")] = (
                found.findtext("description") or ""
            ).strip()
        for found in root.findall("./members/member"):
            self.members[found.get("name")] = (found.text or "").strip()
        for found in root.findall("./signals/signal"):
            self.signals[found.get("name")] = (
                found.findtext("description") or ""
            ).strip()
        for found in root.findall("./constants/constant"):
            self.constants[found.get("name")] = (found.text or "").strip()

    @classmethod
    def load(cls, directory: Path) -> dict[str, "Docs"]:
        return {path.stem: cls(path) for path in sorted(directory.glob("*.xml"))}


MEMBER_ID = re.compile(r'<member name="([A-Z]):Godot\.([^"(]+)')


class SharpDocs:
    """Every name GodotSharp publishes, read from its own documentation file.

    A reference to an engine class cannot be spelled by rule. Godot exposes
    some of its properties as a C# method pair, an enum constant nests under
    a type name the constant does not carry, and a getter often becomes a
    property. GodotSharp ships the answer for all of them, so this reads it
    rather than guessing, and a lookup that misses falls back to text.
    """

    KINDS = ("M", "P", "F", "E", "T")

    def __init__(self, text: str):
        self.paths: dict[str, set[str]] = {kind: set() for kind in self.KINDS}
        self.fields_by_owner: dict[str, set[str]] = {}
        for kind, spelled in MEMBER_ID.findall(text):
            if kind not in self.paths:
                continue
            self.paths[kind].add(spelled)
            if kind == "F" and spelled.count(".") == 2:
                owner, held, leaf = spelled.split(".")
                self.fields_by_owner.setdefault(owner, set()).add(
                    "%s.%s" % (held, leaf)
                )

    @classmethod
    def load(cls, path: Path) -> "SharpDocs":
        return cls(path.read_text(encoding="utf-8"))

    def has(self, kind: str, spelled: str) -> bool:
        return spelled in self.paths[kind]

    def first(self, kinds: str, candidates: list[str]) -> str | None:
        for spelled in candidates:
            for kind in kinds:
                if spelled in self.paths[kind]:
                    return spelled
        return None

    def engine_member(self, owner: str, kind: str, leaf: str) -> str | None:
        """What `ci/csharp.py names` writes into the committed table. Every
        candidate below is a spelling Godot might have chosen, and the index
        is what decides which one it did."""
        spelled = pascal_leaf(leaf)
        direct = "%s.%s" % (owner, spelled)
        if kind == "signal":
            return direct if self.has("E", direct) else None
        if kind == "enum":
            return direct if self.has("T", direct) else None
        if kind == "constant":
            if self.has("F", direct):
                return direct
            return self.nested_constant(owner, leaf)
        bare = leaf
        for opening in ("get_", "set_", "is_"):
            if bare.startswith(opening):
                bare = bare[len(opening):]
                break
        return self.first(
            "MPF",
            [direct, "%s.%s" % (owner, pascal_leaf(bare)),
             "%s.Get%s" % (owner, spelled)],
        )

    def nested_constant(self, owner: str, name: str) -> str | None:
        """One engine enum constant, whose C# name nests under its enum and
        drops the prefix its siblings share.

        The dropped prefix is what names the enum, and requiring that is the
        whole correctness of this search. UPDATE_ALWAYS matches both
        `ClearMode.Always` and `UpdateMode.Always` on the leaf alone, and the
        first in sorted order is the wrong one and compiles.
        """
        held = self.fields_by_owner.get(owner)
        if not held:
            return None
        parts = name.split("_")
        for start in range(len(parts)):
            leaf = pascal_leaf("_".join(parts[start:]))
            dropped = pascal_leaf("_".join(parts[:start]))
            matched = sorted(
                one for one in held
                if one.split(".")[-1] == leaf
                and one.split(".")[0].startswith(dropped)
            )
            if matched:
                return "%s.%s" % (owner, matched[0])
        return None


REFERENCE = re.compile(
    r"\[(method|member|signal|constant|enum|param|theme_item)\s+([@\w\./]+)\]"
)
BARE_TYPE = re.compile(r"\[([A-Z]\w*(?:\.\w+)*)\]")
INLINE_CODE = re.compile(r"\[code(?:\s[^\]]*)?\](.*?)\[/code\]", re.S)
BLOCKS = re.compile(r"\[codeblocks\](.*?)\[/codeblocks\]", re.S)
CSHARP = re.compile(r"\[csharp[^\]]*\](.*?)\[/csharp\]", re.S)
BLOCK = re.compile(r"\[codeblock(?!s)([^\]]*)\](.*?)\[/codeblock\]", re.S)
EMPHASIS = re.compile(r"\[/?(?:u|s|center)\]|\[/?url[^\]]*\]")
SENTINEL = "\x00%d\x00"


def escape(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def pascal_leaf(name: str) -> str:
    """One engine member name in GodotSharp's spelling, which is Godot's own
    snake case turned over word by word."""
    return "".join(piece.capitalize() for piece in name.split("_") if piece)


class Prose:
    """Rewrites one description's BBCode as C# documentation XML.

    A reference to this addon's own surface resolves through the generator's
    naming, recorded while each member was emitted, because the C# spelling
    is decided there and a guess would drift from it. Anything with no C#
    spelling degrades to monospace text, which matters because the compile
    check treats warnings as errors and an unresolvable cref is a warning.
    """

    def __init__(
        self,
        exports: dict,
        classes: set,
        engine: dict,
        builtins: dict,
        names: dict,
    ):
        self.exports = exports
        self.classes = classes
        self.engine = engine
        self.builtins = builtins
        self.names = names
        self.fallbacks: list[str] = []
        self.blocks_carried = 0
        self.gdscript_twins_dropped = 0

    def type_of(self, name: str) -> str | None:
        """The C# spelling of one type, or None when nothing can carry a cref
        for it. An array has no cref syntax, so `byte[]` is text."""
        if name in self.classes:
            return name
        if name in self.engine:
            return self.engine[name]
        spelled = self.builtins.get(name)
        return None if spelled is None or "[" in spelled else spelled

    def resolve(self, owner: str, kind: str, target: str) -> str | None:
        """The cref one reference names, or None when nothing carries it.

        This addon's own surface resolves through the naming the emitter
        recorded. Anything GodotSharp owns resolves against GodotSharp's own
        documentation index, because its spelling cannot be derived: a Godot
        property may be a C# method pair, an enum constant nests under a type
        name it does not carry, and a getter often becomes a property.

        A leaf beginning with an underscore never resolves, and that rule is
        load bearing rather than cautious. `Node.Ready` EXISTS as a signal, so
        an index lookup for `_ready` would answer confidently with the wrong
        member and the compiler would accept it. AGENTS.md 3.10 also says a
        published doc should not have cited one.
        """
        pieces = target.split(".")
        leaf = pieces[-1]
        held = pieces[0] if len(pieces) > 1 else ""
        if leaf.startswith("_"):
            return None

        if held in self.classes:
            mapped = self.exports.get(held, {}).get(leaf)
            return None if mapped is None else "%s.%s" % (held, mapped)
        if not held:
            mapped = self.exports.get(owner, {}).get(leaf)
            return None if mapped is None else "%s.%s" % (owner, mapped)
        outer = self.type_of(held)
        if outer is None:
            return None
        return self.engine_member(outer, kind, leaf)

    def engine_member(self, owner: str, kind: str, leaf: str) -> str | None:
        """One member of a class or struct GodotSharp publishes, read from the
        committed table rather than spelled by rule, because Godot's C#
        spelling cannot be derived from its own name."""
        return self.names.get("%s %s.%s" % (kind, owner, leaf))

    def render(self, owner: str, text: str, params: dict | None = None) -> str:
        """One description as documentation XML, with every payload stashed
        behind a sentinel so escaping the prose cannot touch it."""
        if not text:
            return ""
        params = params or {}
        kept: list[str] = []

        def stash(payload: str) -> str:
            kept.append(payload)
            return SENTINEL % (len(kept) - 1)

        def carried(body: str) -> str:
            self.blocks_carried += 1
            dedented = textwrap.dedent(body.strip("\n")).rstrip()
            return stash(
                "<code>\n%s\n</code>"
                % "\n".join(escape(one.rstrip()) for one in dedented.split("\n"))
            )

        def on_blocks(match: re.Match) -> str:
            """Godot's dual-language form. The C# twin is the one a C# caller
            wants, so it survives and the GDScript twin does not."""
            twin = CSHARP.search(match.group(1))
            self.gdscript_twins_dropped += 1
            return carried(twin.group(1)) if twin else ""

        def on_block(match: re.Match) -> str:
            return carried(match.group(2))

        def on_reference(match: re.Match) -> str:
            kind, target = match.group(1), match.group(2)
            if kind == "param":
                spelled = params.get(target)
                if spelled is None:
                    return stash("<c>%s</c>" % escape(target))
                return stash('<paramref name="%s"/>' % spelled.lstrip("@"))
            found = self.resolve(owner, kind, target)
            if found is None:
                self.fallbacks.append("%s [%s %s]" % (owner, kind, target))
                return stash("<c>%s</c>" % escape(target))
            return stash('<see cref="%s"/>' % found)

        def on_type(match: re.Match) -> str:
            name = match.group(1)
            spelled = self.type_of(name)
            if spelled is None:
                self.fallbacks.append("%s [%s]" % (owner, name))
                return stash("<c>%s</c>" % escape(name))
            return stash('<see cref="%s"/>' % spelled)

        text = BLOCKS.sub(on_blocks, text)
        text = BLOCK.sub(on_block, text)
        text = INLINE_CODE.sub(
            lambda m: stash("<c>%s</c>" % escape(m.group(1).strip())), text
        )
        text = REFERENCE.sub(on_reference, text)
        text = BARE_TYPE.sub(on_type, text)
        text = EMPHASIS.sub("", text)
        for opening, closing in (("b", "b"), ("i", "i")):
            text = text.replace("[%s]" % opening, stash("<%s>" % opening))
            text = text.replace("[/%s]" % closing, stash("</%s>" % closing))
        text = text.replace("[br]", "\n")
        text = escape(text)
        for index, payload in enumerate(kept):
            text = text.replace(escape(SENTINEL % index), payload)
        return text


def paragraphs(text: str) -> list[str]:
    """One description split into the blocks a doc comment lays out. A code
    block carries blank lines of its own and is never torn on one."""
    blocks: list[str] = []
    for piece in re.split(r"(<code>.*?</code>)", text.strip(), flags=re.S):
        if piece.startswith("<code>"):
            if blocks:
                blocks[-1] += "\n" + piece
            else:
                blocks.append(piece)
            continue
        for chunk in re.split(r"\n\s*\n", piece):
            if chunk.strip():
                blocks.append(chunk.strip())
    return blocks


HELD_SPACE = "\x01"
TAG = re.compile(r"<[^>]+>")


def reflow(source: str, room: int) -> list[str]:
    """One run of prose wrapped to the column limit. A tag is one word, so a
    break can never land between `<see` and its `cref`."""
    guarded = TAG.sub(lambda m: m.group(0).replace(" ", HELD_SPACE), source)
    words = guarded.split()
    if not words:
        return []
    lines = [words[0]]
    for word in words[1:]:
        bare = len(lines[-1].replace(HELD_SPACE, " "))
        if bare + 1 + len(word.replace(HELD_SPACE, " ")) <= room:
            lines[-1] += " " + word
        else:
            lines.append(word)
    return [one.replace(HELD_SPACE, " ") for one in lines]


def wrap(text: str, indent: str) -> list[str]:
    """One block as `///` lines, word wrapped to the column limit, with a code
    block's own lines left exactly as they are and a list row kept whole."""
    lines: list[str] = []
    room = WIDTH - len(indent) - 4
    for piece in re.split(r"(<code>.*?</code>)", text, flags=re.S):
        if not piece.strip():
            continue
        if piece.startswith("<code>"):
            lines.extend(piece.split("\n"))
            continue
        run: list[str] = []
        for source in piece.split("\n"):
            stripped = source.strip()
            if not stripped:
                continue
            if stripped.startswith("- "):
                lines.extend(reflow(" ".join(run), room))
                run = []
                lines.extend(reflow(stripped, room))
                continue
            run.append(stripped)
        lines.extend(reflow(" ".join(run), room))
    return [
        "%s/// %s" % (indent, one) if one else "%s///" % indent for one in lines
    ]


def tagged(indent: str, tag: str, text: str) -> list[str]:
    lines = ["%s/// <%s>" % (indent, tag)]
    for index, piece in enumerate(paragraphs(text)):
        if index:
            lines.append("%s/// <para>" % indent)
        lines.extend(wrap(piece, indent))
        if index:
            lines.append("%s/// </para>" % indent)
    lines.append("%s/// </%s>" % (indent, tag))
    return lines


def block(indent: str, summary: str, remarks: str = "") -> list[str]:
    """One whole `///` comment, or nothing when there is no prose for it."""
    lines: list[str] = []
    if summary:
        lines.extend(tagged(indent, "summary", summary))
    if remarks:
        lines.extend(tagged(indent, "remarks", remarks))
    return lines
