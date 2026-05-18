"""Cross-references into the online Godot class reference.

Defines a single ``:godot:`` role used by RST emitted from ``make_rst.py`` (and
hand-written prose) to link to ``docs.godotengine.org``. Centralising URL
construction here keeps ``make_rst.py`` close to upstream — the patches only
need to emit role syntax, not URLs.

Syntax::

    :godot:`Node`                        -> link to the Node class page
    :godot:`Display <Class>`             -> custom display text
    :godot:`Display <Class#fragment>`    -> link with an explicit fragment

The fragment, when provided, is used verbatim. Callers are responsible for
producing fragments that match Godot's URL scheme (e.g. ``class_node_method_get_parent``).

If Godot ever changes the docs URL scheme, edit ``DOCS_BASE`` (and the slug
rule, if needed) — nothing else.
"""

from __future__ import annotations

from typing import Any

from docutils import nodes
from sphinx.application import Sphinx
from sphinx.util.docutils import SphinxRole

DOCS_BASE = "https://docs.godotengine.org/en/stable/classes"


def _class_slug(class_name: str) -> str:
    # Mirrors make_rst.sanitize_class_name(...).lower(): strip the @ that
    # marks pseudo-classes like @GlobalScope / @GDScript.
    return class_name.replace("@", "").lower()


class GodotRole(SphinxRole):
    def run(self) -> tuple[list[nodes.Node], list[nodes.system_message]]:
        text = self.text
        if text.endswith(">") and "<" in text:
            display, _, target = text[:-1].rpartition("<")
            display = display.rstrip()
        else:
            display = text
            target = text

        class_name, _, fragment = target.partition("#")
        url = f"{DOCS_BASE}/class_{_class_slug(class_name)}.html"
        if fragment:
            url = f"{url}#{fragment}"

        node = nodes.reference(
            rawtext=self.rawtext,
            text=display,
            refuri=url,
            classes=["godot-xref", "external"],
        )
        return [node], []


def setup(app: Sphinx) -> dict[str, Any]:
    app.add_role("godot", GodotRole())
    return {
        "version": "0.1",
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
