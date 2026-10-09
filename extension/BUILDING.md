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

Build the library with its tests, then run them through Godot. The GdUnit
suites under `tests/` need this build too.

```sh
scons -C extension netw_tests=yes
godot --headless --fixed-fps 60 --script tests/native/run_native_tests.gd
```

Arguments after `--` pick cases and raise the log level.

```sh
godot --headless --fixed-fps 60 --script tests/native/run_native_tests.gd \
  -- --native-filter="*Promise*" --netw-log=trace
```

The run ends with a summary of the case count, the failing cases, the cases
that ended holding objects and the most common warnings. A warning is
expected when the case that caused it asserted it, and only the unexpected
ones are ranked. Every case is in
`reports/native/results.xml` and every held-object row is in
`reports/native/census.txt`.

Run the GdUnit suites from the editor's GdUnit panel, or from a shell.

```sh
godot --path . -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless \
  --ignoreHeadlessMode --ignore-error-breaks -rc 1 -rd res://reports \
  -a res://tests
```

GdUnit reads errors from the Godot log file, so run one Godot process per
checkout while it runs. A second checkout of this repository gets its own log
from an `override.cfg` in its root.

```ini
[debug]
file_logging/log_path="/absolute/path/to/checkout/tmp/logs/godot.log"
```

## Engine module

Contributors can also compile the same sources into the pinned Godot engine.

```sh
extension/tools/setup_godot.sh
scons -C extension/thirdparty/godot -j"$(nproc)" target=editor tests=yes
```

Set `GODOT_SRC=/path/to/godot` before running the setup script to use an
existing engine checkout.
