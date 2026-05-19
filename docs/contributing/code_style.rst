.. _doc_contributing_code_style:

Code style
==========

Networked follows the conventions you see in the standard library and in
godot-docs' own examples, with a few addon-specific rules to keep the
network-facing parts predictable. This page is not a long list of
formatting rules -- the existing files in ``addons/networked/`` are the
canonical reference -- but it captures the choices that come up in code
review often enough to be worth writing down.

GDScript conventions
--------------------

- **Tabs for indentation**, four-column width. This matches the engine's
  defaults and the editor's auto-formatter.
- **Static typing everywhere a value is assigned at declaration**. The
  inferred form ``var foo := 3`` is preferred over the explicit
  ``var foo: int = 3`` when the right-hand side carries the type. For
  fields, prefer the explicit form so the inspector and the docs see the
  type even when no initializer is present.
- **Use ``class_name``** for any script that is going to be referenced from
  another file. Networked's class reference picks the class up
  automatically once it has a ``class_name``.
- **Doc comments use ``##``**. The first ``##`` block above the class
  declaration becomes the class description in the reference; the block
  above a method or member becomes its description. The first sentence
  should be a complete one-line summary that reads sensibly in a class
  index.
- **Signals are documented**, even when their name seems obvious. The
  reader of the class reference does not always have the script open;
  spell out *who* emits the signal and *when*.

A typical script header looks like this:

.. code-block:: gdscript

    ## Stores per-peer scratch state used by a single component.
    ##
    ## Each consumer declares its own [code]Bucket[/code] inner class and
    ## retrieves a typed instance via [method get_bucket].
    class_name MyComponent
    extends Node

    ## Emitted on the server when [method begin] finishes resolving.
    signal began(result: Dictionary)

RPC discipline
--------------

RPCs are the single largest source of multiplayer bugs, so Networked applies
two extra rules on top of the defaults Godot enforces.

- **Validate the sender, always.** Every ``any_peer`` RPC handler must call
  ``multiplayer.get_remote_sender_id()`` and reject calls from peers it does
  not expect. Use the helper logging pattern from the existing
  :ref:`MultiplayerTree <class_MultiplayerTree>` RPCs for consistency:

  .. code-block:: gdscript

      @rpc("any_peer", "call_local", "reliable")
      func _rpc_request_thing() -> void:
          if not multiplayer.is_server():
              Netw.dbg.warn(
                  "_rpc_request_thing received on non-server peer %d",
                  [multiplayer.get_unique_id()]
              )
              return
          # ...

- **Prefer ``rpc_id(1, ...)`` over broadcast for any client-to-server
  message.** The default ``rpc()`` broadcasts to every peer; for a request
  meant for the server only, that is wasted bandwidth and a security risk.
  The class docs on :ref:`Netw <class_Netw>` call this out for the same
  reason.

Naming
------

- Public methods are ``snake_case``. Helpers and constants follow the engine
  style.
- RPC handlers are prefixed with ``_rpc_`` when they are not part of the
  public API. The prefix makes it obvious in logs which call is an RPC
  versus a normal method.
- Signals are past-tense verbs (``configured``, ``player_joined``) when
  they describe completed events, and present participles
  (``connecting``, ``spawning``) when they describe events in progress.

Tests
-----

The :ref:`development setup <doc_contributing_setup>` page describes the
test framework. A few stylistic notes:

- **One concept per test.** Each ``test_*`` method should assert one
  observable property of the system. Tests that chain three behaviours
  together produce vague failure messages.
- **Name tests for behaviour, not implementation.**
  ``test_client_is_online_after_connect_player`` reads better than
  ``test_connect_player_sets_state_to_online``. The reader of a failing
  test wants to know what is broken, not what line broke.
- **Use the harness for multi-tree cases.** ``NetworkTestHarness`` (in
  ``tests/helpers/``) abstracts the most common server-plus-N-clients
  pattern. Reach for it before hand-rolling a new fixture; if it is
  missing something, extend it instead of duplicating it.

Commits and pull requests
-------------------------

- Keep commits focused. A single PR should usually contain one
  conceptually-coherent change; if it touches docs, code, and tests, that
  is fine as long as they all describe the same change.
- The commit message subject line should fit in 72 characters and read as
  an instruction: ``Make SpawnerComponent log instead of crash on missing
  owner``, not ``Fixed crash``.
- Pull requests should describe the *user-visible* change in the
  description. The body of the PR ends up in the changelog; reviewers (and
  future you) read it before they read the diff.

Thanks for taking the time to read this far. If anything in this page is
out of date or unclear, opening a PR that fixes the page itself is the
single most valuable contribution you can make right now.
