# Building the native library

From a fresh checkout:

```sh
cd extension
scons
```

The first build fetches the exact godot-cpp and Tracy revisions in
`deps.env`. Later builds are offline and incremental. The artifact and
manifest are deployed to `addons/networked/bin/`.

That directory is git-ignored, and the addon's classes now live in the
library, so the project does not open without one. Building it is the first
step after cloning, not an optional one. The binaries stay out of the
repository because a committed manifest without a library for every platform
breaks project load for anyone who has not built one; CI builds every
platform it supports and publishes them as an addon zip instead.

Linux links the system C++ runtime. A private static libstdc++ inside the
shared extension invalidates doctest's locale facets when Godot loads it.

Useful options:

```sh
scons compiledb=yes
scons target=editor dev_build=yes
scons netw_tests=yes
scons netw_profiling=yes
```

Instrumentation is opt-in. A library carrying Tracy's client does not come back
from an editor hot reload: the classes reload with no methods and nothing
reports an error. Asking for `netw_profiling=yes` therefore deploys a manifest
with `reloadable = false`, so a build is either reloadable or instrumented and
never both. Profiling the engine and the addon together is better served by the
module build below, which is linked into the engine and never unloaded.

Put personal SCons defaults in `custom.py`. The file is ignored. Build profiles
use godot-cpp's `build_profile=path.json` option.

## The embedded test suite

`netw_tests=yes` compiles the doctest cases under `tests/` into the library
and adds the one class a driver needs to reach them. Run them through Godot,
since the cases exercise registered classes:

```sh
scons netw_tests=yes
cd .. && godot --headless --script tests/native/run_native_tests.gd
```

The run writes JUnit to `reports/native/results.xml` and exits non-zero on a
failure. Pass `-- --native-filter=<pattern>` to scope it to one file.

## Building as an engine module

The same sources also build as a built-in engine module, which is how they are
checked against the engine's own headers rather than godot-cpp's.
`setup_godot.sh` clones the engine pinned in `deps.env` and mounts the module:

```sh
extension/tools/setup_godot.sh
scons -C extension/thirdparty/godot -j"$(nproc)" target=editor tests=yes
```

Set `GODOT_SRC=/path/to/godot` to mount into an existing engine checkout
instead. The script warns when that checkout is not at the pinned ref, since
the module is only certified against the pin. On Windows, run it from a shell
with symlinks enabled (Git Bash with Developer Mode).

The cases under `tests/` are auto-discovered by the engine's test runner:

```sh
extension/thirdparty/godot/bin/godot.linuxbsd.editor.x86_64 \
  --headless --test --test-case='*[Networked]*'
```

Engine builds are compile-bound, and a build option that reaches the global
environment invalidates every source. Two flags are worth setting:

```sh
scons -C extension/thirdparty/godot -j"$(nproc)" target=editor tests=yes \
  cpp_compiler_launcher=ccache c_compiler_launcher=ccache linker=mold
```

`ccache` is what makes a changed build option cheap, since a define or include
path no source references still hits. Adding it rewrites `CXX`, so the build
that introduces it recompiles everything once.

Add `profiler=tracy profiler_path=$PWD/extension/thirdparty/tracy` to put
engine, addon, and script zones on one Tracy timeline, and `debug_symbols=yes`
for sampled callstacks. A module shares the engine's binary and therefore its
single Tracy client, so instrumentation follows the engine's setting and there
is no separate switch. Never point a Tracy profiler at a `profiler=tracy`
engine that has also loaded the GDExtension build of this addon: that is two
clients in one process.

Build the Tracy server from the checkout in `extension/thirdparty/tracy`, since
client and server must be the same version and `deps.env` may pin a different
one than the engine's documentation uses. Engines before 4.7 have no
`profiler_record_on_demand`, so their client records from launch into memory
whether or not a server is attached. Connect before starting a long session.

## The determinism floor

Both build entries refuse `-ffast-math` and its family, and both compile with
`-ffp-contract=off`. The refusal reads the SCons variables, the shell's own
`CXXFLAGS`, and any flag list already appended, because a flag arrives by three
doors and a guard watching one reads green against the other two.

```sh
scons CXXFLAGS=-ffast-math      # refused, naming the flag
CXXFLAGS=-ffast-math scons      # refused, naming the flag
```

The reason is narrower than "floating point is delicate". Prediction certifies
its port by byte-diffing a golden journal against the GDScript arm, and that
arm contracts nothing. A fused multiply-add is a different answer from a
multiply and an add, so contraction does not fail a case, it invalidates the
instrument that licenses the crossing.

**The floor these flags defend is measured on one platform, one engine build
and one Jolt build.** Four OS processes produced byte-identical float32 traces
there, and nothing claims that across platforms or engine versions. The flags
are the static guard; the golden journal run against the native build is the
evidence, and it is what a family re-measures when its engine crosses.

## Formatting

`.clang-format` and `.pre-commit-config.yaml` at the repository root define
the formatting the `Static Checks` workflow enforces. Install the hooks once
and a commit is already in the shape CI expects:

```sh
pipx install pre-commit   # or: pip install pre-commit
pre-commit install
```

The same hooks run on demand with `pre-commit run --all-files`. Both are
version-pinned so that a formatter upgrade is a deliberate edit rather than a
diff that appears on somebody else's machine.
