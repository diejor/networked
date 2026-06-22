.. _doc_manual_context_and_services:

Context and services
====================

The :ref:`Netw <class_Netw>` static class is the public surface most user
code interacts with day to day. Its centerpiece is
:ref:`Netw.ctx() <class_Netw_method_ctx>`, a single call that resolves a
node's session, services, scene, and entity facades in one go for the give :godot:`Node <Node>`. Once you
get used to reaching for :ref:`Netw.ctx(self) <class_Netw_method_ctx>` first, the rest of the addon
stops feeling like four separate libraries and starts feeling like one.

The four facades
----------------

:ref:`NetwContext <class_NetwContext>` exposes four members, each of which
may independently be ``null`` depending on where in the tree you called it:

- :ref:`tree <class_NetwContext_property_tree>`: a
  :ref:`NetwTree <class_NetwTree>` facade that wraps the enclosing
  :ref:`MultiplayerTree <class_MultiplayerTree>`. Use it for session-level
  operations: pause, unpause, kick, request disconnect.
- :ref:`services <class_NetwContext_property_services>`: a service
  locator for backend systems registered on the tree. The built-in
  services include the :ref:`MultiplayerClock <class_MultiplayerClock>` and the
  :ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>`. You can
  add your own with
  :ref:`NetwServices.register() <class_NetwServices_method_register>`.
- :ref:`scene <class_NetwContext_property_scene>`: a
  :ref:`NetwScene <class_NetwScene>` facade for the enclosing
  :ref:`MultiplayerScene <class_MultiplayerScene>`. Provides readiness
  gates, countdowns, and ready/waiting-room flows.
- :ref:`entity <class_NetwContext_property_entity>`: a
  :ref:`NetwEntity <class_NetwEntity>` facade resolved by walking from the
  origin node up to the owning entity root. Available even on orphan nodes
  (during :godot:`NOTIFICATION_PARENTED <Node#class_node_constant_notification_parented>`,
  for example), which is why it is the only facade you can rely on inside
  the very early parts of a spawn lifecycle.

The split is intentional. Tree and services need an enclosing
:ref:`MultiplayerTree <class_MultiplayerTree>`. Scene needs an enclosing
:ref:`MultiplayerScene <class_MultiplayerScene>`, entity needs only a
parent chain. Code that reads only what it needs stays usable in more
contexts. A component that resolves :ref:`ctx.entity <class_NetwContext_property_entity>` works in editor
tests, in headless integration tests, and in unspawned templates without
any guards beyond a null check on the member it actually uses.

.. tip::

    Always check :ref:`is_valid() <class_NetwContext_method_is_valid>`
    before caching a context across frames. The underlying tree can be
    freed during disconnects and scene swaps. A stale context returns
    facades that look healthy but point at freed objects.

Registering custom services
---------------------------

Services are :godot:`Node <Node>` instances that live under the
:ref:`MultiplayerTree <class_MultiplayerTree>` and are looked up by type.
Networked already registers the clock and the scene manager. To add your
own, declare the class and register the instance:

.. tabs::
 .. code-tab:: gdscript GDScript

    class_name BomberGamestate
    extends Node

    func _enter_tree() -> void:
        NetwServices.register(self)

    func _exit_tree() -> void:
        NetwServices.unregister(self)

Anywhere in the session, recover the service with the typed accessor:

.. tabs::
 .. code-tab:: gdscript GDScript

    var ctx := Netw.ctx(self)
    var gamestate: BomberGamestate = ctx.services.get_service(BomberGamestate)
    gamestate.begin_match()

Services must be descendants of the tree node. The tree asserts on this
because it owns the service registry's lifetime. If you need a service
that is reachable from multiple trees (a shared online-services
abstraction, for example) make it a singleton autoload and have a per-tree
service node forward to it.

Logging via Netw.dbg
--------------------

The same :ref:`Netw <class_Netw>` namespace exposes
:ref:`dbg <class_Netw_property_dbg>`, a structured logger that classifies
messages by severity, attaches scope handles to nodes, and tags every line
with the originating tree id. The minimum useful pattern is:

.. tabs::
 .. code-tab:: gdscript GDScript

    var _dbg: NetwHandle = Netw.dbg.handle(self)

    func _ready() -> void:
        _dbg.info("Spawned in scene %s", [get_parent().name])

The handle keeps a weak reference to the node, so it is safe to assign
from class-level fields without leaking. Levels follow the usual ordering:
:ref:`TRACE <class_NetwLog_constant_TRACE>` for "every frame is fine", :ref:`DEBUG <class_NetwLog_constant_DEBUG>` for development noise,
:ref:`INFO <class_NetwLog_constant_INFO>` for one-line per session events, :ref:`WARN <class_NetwLog_constant_WARN>` for recoverable
mis-wirings, :ref:`ERROR <class_NetwLog_constant_ERROR>` for "this session is now broken". The default
level is :ref:`INFO <class_NetwLog_constant_INFO>`. Switch to :ref:`DEBUG <class_NetwLog_constant_DEBUG>` while debugging join issues and you
will see the full handshake annotated step-by-step in the output panel.

When to call Netw.ctx
---------------------

``Netw.ctx(node)`` is cheap. It walks the parent chain and constructs a
small :ref:`NetwContext <class_NetwContext>` wrapper. Call it on demand at
the top of methods that need session access rather than caching it on the
node, unless you have a profiler-measured reason not to. Caching is fine
inside a method, but a stale field across frames is the most common cause
of "I'm reading the wrong tree" bugs after a reconnect.

If you find yourself reaching for the same facade in many methods of the
same node, the canonical shortcut is to cache the *facade*, not the
context, in :godot:`_ready() <Node#class_node_private_method__ready>` after the surrounding tree is configured:

.. tabs::
 .. code-tab:: gdscript GDScript

    @onready var ctx: NetwContext = Netw.ctx(self)

    func _on_player_joined(rj: ResolvedJoin) -> void:
        if not ctx.is_valid():
            ctx = Netw.ctx(self)
        ctx.services.get_service(BomberGamestate).register_player(rj)

This keeps you inside the addon's lifetime guarantees without writing a
re-resolve helper for every script that needs the tree.
.
