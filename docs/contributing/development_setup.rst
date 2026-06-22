.. _doc_contributing_setup:

Development setup
=================

Clone the repository and open its root folder as a Godot project:

.. code-block:: console

    git clone https://github.com/diejor/networked.git
    cd networked

The project already contains the addon, examples, tests, and documentation.
Godot will import resources the first time you open it. After import finishes,
make sure the *Networked* and *GdUnit4* plugins are enabled in
:menu:`Project > Project Settings > Plugins`.

The project is currently developed and tested with the Godot version used by
CI. See ``.github/workflows/ci.yml`` and
``.github/actions/build-docs-classes/action.yml`` for the exact versions used
by tests and documentation builds.

Running tests
-------------

Networked uses `GdUnit4 <https://mikeschulze.github.io/gdUnit4/>`__. Run the
tests from the editor with :menu:`Tools > GdUnit4 > Run Tests`, or from the
command line:

.. warning::

    Run tests with one Godot instance. Disable
    :menu:`Debug > Run Multiple Instances` and close any extra debug sessions
    before starting GdUnit4. The integration tests manage their own peers and
    will fail if another project instance is already running.

.. tabs::
 .. code-tab:: bash Linux/macOS

    ./addons/gdUnit4/runtest.sh -a tests

 .. code-tab:: powershell Windows

    .\addons\gdUnit4\runtest.cmd -a tests

Use a focused path while working on one area:

.. tabs::
 .. code-tab:: bash Linux/macOS

    ./addons/gdUnit4/runtest.sh -a tests/integration/test_multiplayer_tree_connect.gd

 .. code-tab:: powershell Windows

    .\addons\gdUnit4\runtest.cmd -a tests/integration/test_multiplayer_tree_connect.gd

The test session hook can enable Networked logs for the whole run:

.. tabs::
 .. code-tab:: bash Linux/macOS

    NETW_TEST_LOG=trace ./addons/gdUnit4/runtest.sh -a tests

 .. code-tab:: powershell Windows

    $env:NETW_TEST_LOG = "trace"
    .\addons\gdUnit4\runtest.cmd -a tests

You can also pass ``--netw-log=trace`` to Godot, or enable logs for a single
test from a GdUnit test class:

.. code-block:: gdscript

    func before_test() -> void:
        enable_logs("trace")

Use logs when debugging session setup, backend connection failures, spawn
order, or authority issues. Leave them disabled for normal test runs unless
the failure needs the extra detail.

Running examples
----------------

The repository includes example scenes that exercise the addon directly:

- ``examples/quick_start/Main.tscn`` shows spawning, saving, and teleporting.
- ``examples/bomber/main.tscn`` shows a lobby, multiple players, and gameplay
  over a backend.

Open a scene in the editor and press :kbd:`F5`. To test more than one local
peer, use :menu:`Debug > Run Multiple Instances`.
