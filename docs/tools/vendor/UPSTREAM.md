# Vendored upstream files

This directory contains pristine copies of files from the Godot engine
repository. They are **never** edited in place. Modifications live in
`../patches/` and are applied by `../build_make_rst.py`.

## Source

- **File:** `make_rst.py`
- **Origin:** <https://raw.githubusercontent.com/godotengine/godot/refs/heads/4.6/doc/tools/make_rst.py>
- **Branch:** `4.6`
- **Pinned at:** branch tip on 2026-05-15

## Refreshing the vendor copy

```sh
curl -sSL https://raw.githubusercontent.com/godotengine/godot/refs/heads/4.6/doc/tools/make_rst.py \
  -o tools/vendor/make_rst.py

python tools/build_make_rst.py            # re-apply patches; fix rejects if any
```

If the upstream file moved on and patches no longer apply cleanly, edit the
patch in `../patches/` (or rerun `tools/regen_patches.py` if you prefer to
edit the working `tools/make_rst.py` and regenerate).
