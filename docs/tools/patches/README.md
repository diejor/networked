# `make_rst.py` patches

Networked-specific modifications to upstream Godot's `make_rst.py` live here as
unified-diff patch files. They are applied **in lexical order** to
`../vendor/make_rst.py` by `../build_make_rst.py`, which writes the result to
`../make_rst.py`.

```
upstream 4.6 make_rst.py          (vendored, never edited)
        +
patches/*.patch  (applied in order)
        ↓
make_rst.py      (generated; not committed)
```

The generated file is gitignored — always run `python tools/build_make_rst.py`
after pulling. CI checks `--check` to fail the build if the working tree
diverges.

## Patch index

| # | File | Purpose |
|---|------|---------|
| 0001 | `0001-standalone-addon-pathing-and-defaults.patch` | Run against an addon-only XML set: fix `sys.path` for `docs/tools/`, replace the hard-coded `EDITOR_CLASSES` list with `api_type` from XML, walk inheritance through engine classes that aren't in the local set, add `PRIMITIVE_TYPES`, friendlier warning-summary message, `godot_external_link` helper. |
| 0002 | `0002-external-godot-refs-and-abstract.patch` | Emit `:godot:` roles (resolved by `_extensions/godot_xref.py`) for types and enums that aren't in the local XML set. Adds `|abstract|` substitution for GDScript 2.0 abstract methods, guards the grouped class index against missing engine bases, and matches nested class names (`Foo.Bar.baz`) in cross-reference targets. |
| 0003 | `0003-debug-tag-stack-and-engine-class-tags.patch` | Track BBCode tag depth via `debug_tag_stack` and warn on illegal nesting. Recognise unknown uppercase `[Foo]` tags as engine-class references instead of erroring. |
| 0004 | `0004-ref-resolution-fallback-and-warnings.patch` | When a `[method/member/signal/...]` reference points outside the local set, downgrade the error to a warning and emit an external `:godot:` link instead of a broken `:ref:` anchor. Adds `@GlobalScope` existence guard. |
| 0005 | `0005-tag-handling-and-codeblock-escape.patch` | Ignore `[color]`/`[font]` BBCode tags, guard `[br]` at end-of-text from `IndexError`, fall back uppercase invalid tags to `make_type()`, and escape leading whitespace after `[/codeblock]` with `\ ` so the next paragraph isn't eaten into the literal block. |

## URL construction lives in the Sphinx extension

Patches **never** hard-code `docs.godotengine.org` URLs. They emit
`:godot:`Type``, `:godot:`Display <Class#fragment>`` etc. The URL scheme is
defined exactly once in `docs/_extensions/godot_xref.py`. To follow a future
Godot docs URL change, edit `DOCS_BASE` there — patches don't need touching.

## Workflows

### Editing the working file directly, then regenerating patches

The most ergonomic flow if you have many small changes to make. Edit
`tools/make_rst.py` like a normal file, then regenerate the patches:

```sh
python tools/regen_patches.py
```

This rebuilds the vendor-vs-working diff and re-splits it into the existing
themed patches (using the same line buckets as `build_make_rst.py` references).
Review the result, then commit.

### Editing a patch file directly

For small targeted changes, edit the relevant `.patch` file and run
`tools/build_make_rst.py` to regenerate `make_rst.py`.

### Refreshing the vendored upstream

When picking up a new upstream snapshot:

```sh
curl -sSL https://raw.githubusercontent.com/godotengine/godot/refs/heads/4.6/doc/tools/make_rst.py \
  -o tools/vendor/make_rst.py
python tools/build_make_rst.py
```

If patches no longer apply cleanly, `patch` produces `.rej` files. Inspect and
either update the relevant `.patch` file by hand or do a regen pass from the
working copy.

## Upstream candidates

Several patches are pure bugfixes / portability that could be upstreamed to
godot-docs:

- empty `post_text` guard for `[br]` at end-of-description (in 0005)
- `@GlobalScope` existence check in constant resolution (in 0004)
- guard the grouped class index against missing engine-base entries (in 0002)
- `|abstract|` substitution for GDScript 2.0 abstract methods (in 0002)

If upstream merges any of these, drop the corresponding hunks from the patch
and rebuild.
