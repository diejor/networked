# Building the GDExtension

Networked includes a native library. Build it before opening the project in
Godot.

You need Git, Python 3, SCons, a C++17 compiler, and Godot 4.7 or newer.

From the repository root, run the following command.

```sh
scons -C extension
```

The first build downloads the pinned godot-cpp and Tracy dependencies. The
finished library and its manifest are written to `addons/networked/bin/`.

Run the same command again after changing native code. SCons rebuilds only
what changed.

## Useful options

```sh
scons -C extension target=editor dev_build=yes
scons -C extension compiledb=yes
scons -C extension netw_profiling=yes
```

Put personal SCons defaults in `extension/custom.py`.

## Tests

Build the library with its tests, then run them.

```sh
scons -C extension netw_tests=yes
python3 ci/test.py native
python3 ci/test.py native --filter "*Promise*"
```

Results are written to `reports/native/results.xml`.

The GdUnit suites under `tests/` run from the editor's GdUnit panel, or from
a shell.

```sh
python3 ci/test.py gdunit --name tests --path res://tests
```

## Engine module

Contributors can also compile the same sources into the pinned Godot engine.

```sh
extension/tools/setup_godot.sh
scons -C extension/thirdparty/godot -j"$(nproc)" target=editor tests=yes
python3 ci/test.py module
```

Set `GODOT_SRC=/path/to/godot` before running the setup script to use an
existing engine checkout.
