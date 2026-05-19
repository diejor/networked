.. _doc_contributing_code_style:

Code style
==========

Networked follows Godot's GDScript style where possible. Existing files in
``addons/networked/`` are the best reference for formatting, naming, and
structure.

Keep changes small and direct. Multiplayer code is easier to review when a
patch changes one behavior, includes the tests for that behavior, and avoids
rewriting nearby code for style alone.

GDScript
--------

- Use tabs for indentation.
- Prefer wrapping code at 80 columns. This is not a hard formatter rule, but
  long expressions should usually be split before review.
- Prefer typed variables, parameters, return values, and exported fields.
- Use ``class_name`` for scripts that form part of the public API or are used
  from other files.
- Use ``##`` documentation comments on public classes, methods, properties,
  and signals.
- Keep public names in ``snake_case``. Private helpers should start with
  ``_``.
- Name signals after the event they report, such as ``player_joined`` or
  ``configured``.

Doc comments should explain what the API does from the caller's point of
view. Avoid restating the method name. A useful first sentence can stand alone
in the generated class reference.

.. code-block:: gdscript

    ## Stores per-peer state for one component.
    class_name MyComponent
    extends Node

    ## Emitted on the server after setup finishes.
    signal configured(peer_id: int)

Logging
-------

Use ``Netw.dbg`` for addon logs. Pass formatting values as an array instead
of formatting the string first:

.. code-block:: gdscript

    Netw.dbg.info("Player %s connected.", [username])

The array form avoids formatting work when the log level is disabled. If the
arguments are expensive to compute, check the level first:

.. code-block:: gdscript

    if Netw.dbg.is_level_active(NetwLog.Level.TRACE):
        Netw.dbg.trace("Roster: %s", [build_roster_dump()])

For warnings and errors that should appear in Godot's editor output with a
clickable file and line, pass a callable that emits the engine warning or
error from the call site:

.. code-block:: gdscript

    Netw.dbg.warn(
        "Backend failed to bind port %d.", [port],
        func(m): push_warning(m)
    )
    Netw.dbg.error(
        "Scene '%s' could not be loaded.", [path],
        func(m): push_error(m)
    )

Component code can also cache a handle:

.. code-block:: gdscript

    var _dbg: NetwHandle = Netw.dbg.handle(self)

    func _ready() -> void:
        _dbg.info("Ready for peer %d.", [multiplayer.get_unique_id()])

RPCs
----

Treat every RPC as a public network boundary. Validate who sent it, validate
the state it expects, and log failures with enough context to diagnose the
peer that made the call.

For client-to-server messages, prefer
:godot:`rpc_id(1, ...) <Node#class_node_method_rpc_id>` over broadcast RPCs.
Use :godot:`get_remote_sender_id() <MultiplayerAPI#class_multiplayerapi_method_get_remote_sender_id>`
in ``any_peer`` handlers and reject calls from unexpected peers.

Most Networked RPC handlers that represent client requests use
``@rpc("any_peer", "call_local", "reliable")``. ``call_local`` matters for
listen servers: the server can also be the local player, so a locally
triggered request must run on the same peer. Server-only operations should
still guard with ``multiplayer.is_server()`` inside the handler, because
remote clients can call the same RPC too.

.. code-block:: gdscript

    @rpc("any_peer", "call_local", "reliable")
    func _rpc_request_spawn() -> void:
        if not multiplayer.is_server():
            Netw.dbg.warn(
                "_rpc_request_spawn received on non-server peer %d",
                [multiplayer.get_unique_id()]
            )
            return

        var sender_id := multiplayer.get_remote_sender_id()
        # Validate sender_id before mutating session state.

Assertions
----------

Use ``assert()`` for invariants that should fail loudly in debug builds and
for time-sensitive code where continuing would hide the real fault. Examples
include server-only entry points, required setup order, and impossible spawn
states.

Do not use ``assert()`` for recoverable user or network errors. Log those with
``Netw.dbg.warn()`` or ``Netw.dbg.error()`` and return an error value when the
caller can handle the failure.

Tests
-----

Add or update tests with code changes whenever the behavior can be checked
without excessive setup. Prefer tests that describe user-visible behavior over
implementation details.

For multiplayer flows, use the helpers in ``tests/helpers/`` before creating a
new fixture. If a helper is missing one small feature, extend the helper rather
than copying setup code into a new test.
