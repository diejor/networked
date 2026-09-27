# `make_rst.py` patches

`docs/tools/make_rst.py` is Godot's upstream `make_rst.py` with the patches in
this directory applied in order. The upstream copy lives in `../vendor/`.

```sh
python3 docs/tools/build_make_rst.py          # rebuild make_rst.py
python3 docs/tools/build_make_rst.py --check  # fail if it does not match
```

## Patches

| Patch | Purpose |
|-------|---------|
| 0001 | Run against the addon's own XML, with engine classes as external bases. |
| 0002 | Link engine types with the `:godot:` role and group the class index by addon folder. |
| 0003 | Warn on bad BBCode nesting and treat unknown `[Foo]` tags as engine classes. |
| 0004 | Link references outside the addon to the engine docs instead of failing. |
| 0005 | Ignore `[color]` and `[font]`, and keep text after a codeblock out of it. |

The `:godot:` role and the engine docs URL are defined in
`docs/_extensions/godot_xref.py`.

## Editing

Edit `docs/tools/make_rst.py` directly, then split the change back into the
patches.

```sh
python3 docs/tools/regen_patches.py
```

To edit a patch by hand, change the `.patch` file and run
`build_make_rst.py`.
