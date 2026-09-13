.. _doc_contributing_pipeline:

Build, test and release pipeline
================================

Everything CI does, it does by calling a script in ``ci/``. The same scripts
run from a developer shell with the same arguments, so a failure can be
reproduced locally without a runner, and a workflow never grows a second copy
of a build command that drifts from the first.

.. code-block:: text

    ci/platforms.json   the supported platforms, their slices and profiles
    ci/engines.json     the Godot builds this project is verified against
    ci/tools.json       pinned actionlint, ShellCheck and act
    ci/matrix.py        a profile becomes an explicit list of library cells
    ci/build.py         builds one cell and records what it produced
    ci/package.py       assembles the installable addon from staged cells
    ci/test.py          the test lanes, each bounded and separately reported
    ci/reports.py       grades a JUnit report and refuses what is not evidence
    ci/export.py        exports a demo and proves the export in a browser
    ci/engine.py        installs and qualifies an engine from the catalog
    ci/changed.py       decides which lanes a change can reach
    ci/release.py       the inventory gate a publication has to pass
    ci/local.py         runs a named local profile and records the evidence

Cells and profiles
------------------

A *cell* is one native compilation, named ``platform:slice:target``. A
*profile* is a named set of cells. ``ci/platforms.json`` owns both:

.. code-block:: console

    python3 ci/matrix.py --profile release --format ids
    python3 ci/matrix.py --cells linux:x86_64:template_debug --format json

``pr`` is what a pull request builds, ``release`` is every supported cell,
and ``local`` is the pair a Linux workstation can build and run. Asking for a
platform, slice or target the catalog does not declare fails immediately
rather than building nothing.

Building
--------

.. code-block:: console

    python3 ci/build.py --cell linux:x86_64:template_debug
    python3 ci/build.py --profile pr --strip --require-fresh

Each cell stages into ``dist/cells/<artifact>/`` beside a ``build.json``
recording the source revision, the effective SCons arguments, the toolchain
and the hash of every output. Packaging reads those records, so a library
built at another revision cannot reach an addon.

Packaging
---------

.. code-block:: console

    python3 ci/package.py --profile local
    python3 ci/package.py --cells linux:x86_64:template_debug \
        --deploy-into addons/networked/bin

The first writes ``dist/networked-<version>.zip``, its manifest and
``SHA256SUMS.txt``. The second populates an addon for a job that needs the
extension to load. Both refuse a missing cell, a cell from another revision,
and two cells claiming one filename with different content.

Testing
-------

Every lane runs under a deadline, writes into a report path of its own, and
is graded rather than counted:

.. code-block:: console

    python3 ci/test.py import
    python3 ci/test.py parse res://tests res://addons/networked
    python3 ci/test.py native
    python3 ci/test.py gdunit --name tests --path res://tests/
    python3 ci/test.py gdunit --name examples --path res://examples/
    python3 ci/reports.py reports/native/results.xml \
        --floor tests/native/hosted_floor.txt

``res://tests`` and ``res://examples`` are always two invocations. One process
carrying both exhausts the deferred-call queue and takes the whole run down.

``ci/reports.py`` refuses a report that is missing, empty, malformed, older
than the library it describes, truncated, or smaller than the corpus recorded
for this revision, as well as one whose cases failed. ``--self-test`` shows it
refusing each of those.

Running a lane locally
----------------------

.. code-block:: console

    python3 ci/local.py checks
    python3 ci/local.py native gdscript
    python3 ci/local.py web
    python3 ci/local.py candidate --act-cell linux:x86_64:template_debug

Each run writes ``dist/evidence.json`` with the revision, the host's
capabilities, and every command with its exit code and duration. ``--act-cell``
additionally rehearses the workflow layer itself through ``act``, which
exercises the real YAML rather than a second local copy of it.

Engines
-------

``ci/engines.json`` is the only place an engine's identity is written down.

.. code-block:: console

    python3 ci/engine.py install
    python3 ci/engine.py --engine stepping install
    python3 ci/engine.py templates --dest .engines/export_templates
    python3 ci/engine.py source branch

An install verifies the archive's checksum, checks that the expected
executable is inside it, compares the version the binary reports to the
catalog, and probes the capabilities the catalog declares. A stepping lane
therefore cannot silently run on a stock editor.

Releasing
---------

A version tag builds every declared cell, assembles one addon, and publishes
it. A manual run of the same workflow produces the same candidate and stops at
the inventory gate:

.. code-block:: console

    python3 ci/release.py --profile release

Publication requires every declared cell present, no platform recorded as
unqualified, a tag naming the version the addon declares, and every asset
hashing to what ``SHA256SUMS.txt`` records. Assets upload to a draft and the
draft flips to public only afterwards, so a failed upload leaves a draft
rather than a release missing half its product. A retry uploads only what is
not there yet; a corrected build gets a new version.

Distribution
------------

The release archive is the addon, shaped to unzip at the root of a Godot
project. It carries ``addons/networked/networked.gdextension`` at the addon
root, with library paths relative to it, so the extension still loads if a
project keeps the folder somewhere other than ``addons/``.

There is one manifest, ``extension/networked.gdextension``. ``ci/package.py``
derives the shipped copy from it by rewriting every ``res://addons/networked/``
path to ``./``, and refuses if any absolute path survives, so the two can
never disagree.

The Asset Library takes a direct download URL rather than only a commit
archive, so an entry points at the release asset:

.. code-block:: text

    download_provider  Custom
    download_url       https://github.com/diejor/networked/releases/
                         download/v<version>/networked-v<version>-godot<x.y>.zip

The engine version is part of the asset name because the Asset Library holds
one entry per Godot version. Nothing is trimmed from the repository's own
source archives, which stay source.

Supported platforms
-------------------

The release profile declares 22 native compilations:

.. code-block:: text

    Linux     x86_64, arm64            debug + release
    Windows   x86_64                   debug + release
    macOS     x86_64, arm64            debug + release, merged per target
    Android   arm64, x86_64            debug + release
    iOS       device arm64, simulator arm64 and x86_64
    Web       wasm32, threads off      debug + release

macOS slices merge into one framework per target and the iOS slices assemble
into an XCFramework, both of which need a Mac. A platform that a run cannot
qualify is named in the manifest's ``unqualified`` list, and the release gate
refuses to publish a candidate that carries one.
