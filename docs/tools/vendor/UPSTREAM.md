# Vendored upstream files

`make_rst.py` here is an unmodified copy of Godot's
[`doc/tools/make_rst.py`](https://raw.githubusercontent.com/godotengine/godot/refs/heads/4.6/doc/tools/make_rst.py)
from the `4.6` branch, taken on 2026-05-15. Changes go in `../patches/`.

To update it, download the new copy and rebuild. If a patch no longer
applies, fix it as described in `../patches/README.md`.

```sh
curl -sSL https://raw.githubusercontent.com/godotengine/godot/refs/heads/4.6/doc/tools/make_rst.py \
  -o docs/tools/vendor/make_rst.py
python3 docs/tools/build_make_rst.py
```
