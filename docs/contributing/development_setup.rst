.. _doc_contributing_setup:

Development setup
=================

This page walks through getting a Networked checkout running locally, both
the addon proper and the test suite. The addon ships as a Godot project that
contains its own examples and tests, so cloning the repository gives you
everything you need to make changes and verify them without a second
project.

Cloning the project
-------------------

The repository uses Git LFS for binary art assets in the example projects.
After cloning, run ``git lfs pull`` once -- otherwise your editor will open
broken textures in the bomber and daily examples.

.. code-block:: console

    git clone https://github.com/diejor/networked.git
    cd networked
    git lfs pull

The directory layout is intentionally flat: ``addons/networked`` is the
addon itself, ``examples/`` contains complete sample projects, ``tests/``
contains the GdUnit4 test suite, and ``docs/`` is the Sphinx documentation
you are reading right now.

Editor and engine versions
--------------------------

Networked targets Godot 4.2 and newer. CI builds with the current stable
release; if a patch starts depending on a feature from a newer minor, the
PR should bump the ``Requirements`` section in ``README.md`` and the
matching note at the top of :ref:`doc_quick_start`.

Open the repository root as a Godot project. The editor will import the
addon's resources on first launch -- give it a minute, particularly on a
fresh clone where the ``.godot`` import cache is empty. Once import
finishes, enable the *Networked* plugin in
:menu:`Project → Project Settings → Plugins` if it is not enabled already.

Running the test suite
----------------------

The repository uses `GdUnit4 <https://mikeschulze.github.io/gdUnit4/>`__,
vendored as an addon under ``addons/gdUnit4``. There are two layers of
tests you should know about:

- **Unit tests** in ``tests/unit/`` exercise individual scripts in
  isolation. They are fast (milliseconds per test) and have no multiplayer
  setup -- they are the right place for new logic that does not touch
  RPCs.
- **Integration tests** in ``tests/integration/`` spin up real
  :ref:`MultiplayerTree <class_MultiplayerTree>` instances, host a session,
  connect one or more clients, and assert end-to-end behaviour. They use
  the :ref:`LocalLoopbackBackend <class_LocalLoopbackBackend>` and the
  ``NetworkTestHarness`` helper to
  avoid touching the network.

From the editor, run the full suite from the *GdUnit4* dock:
:menu:`Tools → GdUnit4 → Run Tests`. From the command line, the addon
ships a runner script:

.. tabs::
 .. code-tab:: bash Linux/macOS

    ./addons/gdUnit4/runtest.sh -a tests

 .. code-tab:: powershell Windows

    .\addons\gdUnit4\runtest.cmd -a tests

If you are adding a feature that touches the session, please write at
least one integration test alongside the unit tests. The existing
``tests/integration/test_multiplayer_tree_connect.gd`` is a good template
to copy: it builds a server tree, a client tree, hosts, joins, and
asserts.

.. note::

    The integration tests assume the GdUnit4 runner is in charge of the
    main loop. If your test creates additional ``SceneTree`` plumbing of
    its own, free it in ``after_test`` -- the ``auto_free`` helper is the
    most reliable way to do this.

Running the examples
--------------------

The fastest way to validate a change to the core is to run the bundled
examples. Both rely on the addon directly without any duplication, so a
regression usually shows up immediately:

- ``examples/daily/Main.tscn`` -- a single-player walk-and-teleport scene
  that covers the core spawn, save, and teleport flow. Good for verifying
  changes to :ref:`SpawnerComponent <class_SpawnerComponent>`,
  :ref:`SaveComponent <class_SaveComponent>`, and
  :ref:`TPComponent <class_TPComponent>`.
- ``examples/bomber/main.tscn`` -- a multi-player lobby and match scene
  using :ref:`WebSocketBackend <class_WebSocketBackend>`. Good for
  verifying changes to the session roster, scene manager, and per-peer
  context plumbing.

Run each scene with :kbd:`F5` after temporarily setting it as the project's
main scene. To test two clients at once, use :menu:`Debug → Run Multiple
Instances`.
