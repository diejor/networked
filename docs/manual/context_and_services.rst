.. _doc_manual_context_and_services:

Sessions and services
=====================

The :ref:`Netw <class_Netw>` static class is the public surface most user
code interacts with day to day. Its centerpiece is
:ref:`Netw.of() <class_Netw_method_of>`, a single call that resolves a node's
:ref:`NetwMultiplayer <class_NetwMultiplayer>` session for the given
:godot:`Node <Node>`. Once you get used to reaching for
:ref:`Netw.of(self) <class_Netw_method_of>` first, the rest of the addon stops
feeling like several separate libraries and starts feeling like one object.

Two rules
---------

There are exactly two ways to ask a question, and which one you use depends on
the kind of question:

- **Positional questions** (what encloses this node?) are static walkers on the
  answering class. :ref:`NetwEntity.of() <class_NetwEntity_method_of>` resolves
  the entity for a node, and
  :ref:`NetwEntity.scene <class_NetwEntity_property_scene>` resolves its
  scene. Both walk the parent chain and work on orphan nodes, which is why they
  are the only ones you can rely on inside the very early parts of a spawn
  lifecycle (during
  :godot:`NOTIFICATION_PARENTED <Node#class_node_constant_notification_parented>`,
  for example).
- **Session questions** (who is live, what is visible, what services exist?)
  are properties and methods on the
  :ref:`NetwMultiplayer <class_NetwMultiplayer>` you reach with
  :ref:`Netw.of(node) <class_Netw_method_of>`. A node that is not inside a
  :ref:`MultiplayerTree <class_MultiplayerTree>` yet has no session, so
  :ref:`Netw.of() <class_Netw_method_of>` returns ``null`` there. That is the
  true semantics: liveness, interest, participants, and services do not exist
  for a node outside a session.

The session surface
-------------------

:ref:`NetwMultiplayer <class_NetwMultiplayer>` is the one object you talk to
for everything about the session you are in:

- session verbs and state:
  :ref:`session.pause() <class_NetwSessionHandle_method_pause>`,
  :ref:`session.leave() <class_NetwSessionHandle_method_leave>`,
  :ref:`peer_kick() <class_NetwMultiplayer_method_peer_kick>`,
  :ref:`role <class_NetwMultiplayer_property_role>`,
  :ref:`participants <class_NetwMultiplayer_property_participants>`,
  :ref:`local_player <class_NetwMultiplayer_property_local_player>`. Bringing a
  session *up* is not here: that is
  :ref:`NetwConnector.of(api) <class_NetwConnector_method_of>`, which is a
  client of this surface rather than part of it.
- owned interfaces:
  :ref:`clock <class_NetwMultiplayer_property_clock>`,
  :ref:`embedding <class_NetwMultiplayer_property_embedding>`,
  :ref:`session <class_NetwMultiplayer_property_session>`,
  :ref:`liveness <class_NetwMultiplayer_property_liveness>`,
  :ref:`interest <class_NetwMultiplayer_property_interest>`,
  :ref:`lag_compensation <class_NetwMultiplayer_property_lag_compensation>`,
  :ref:`interpolation <class_NetwMultiplayer_property_interpolation>`. These are
  direct properties, never ``null`` once the session exists (the clock and lag
  compensation stay inert until a configurator node registers).
- the built-in
  :ref:`scene_manager <class_NetwMultiplayer_property_scene_manager>` service,
  plus your own through
  :ref:`get_service() <class_NetwMultiplayer_method_get_service>`.

Browsing servers before a session starts is a separate, optional object:
construct a :ref:`NetwServerBrowser <class_NetwServerBrowser>` over the session.
It lists and probes targets but cannot start a connection, so picking a row and
joining it stay two different objects' jobs.

Registering custom services
---------------------------

Services are :godot:`Node <Node>` instances that live under the
:ref:`MultiplayerTree <class_MultiplayerTree>` and are looked up by type.
Networked already registers the scene manager. To add your own, declare the
class and register the instance through
:ref:`NetwService <class_NetwService>`:

.. tabs::
 .. code-tab:: gdscript GDScript

    class_name BomberGamestate
    extends Node

    func _enter_tree() -> void:
        NetwService.register(self)

    func _exit_tree() -> void:
        NetwService.unregister(self)

Anywhere in the session, recover the service with the typed accessor:

.. tabs::
 .. code-tab:: gdscript GDScript

    var gamestate: BomberGamestate = Netw.of(self).get_service(BomberGamestate)
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

Caching the session
-------------------

:ref:`Netw.of() <class_Netw_method_of>` returns the tree's persistent
:ref:`NetwMultiplayer <class_NetwMultiplayer>`, created once per tree and stable
across reconnects and backend swaps. Unlike the old per-call context wrapper it
is safe to cache: a cached reference stays a valid object and reports
inactivity through
:ref:`is_active() <class_NetwMultiplayer_method_is_active>` instead of dangling
after teardown.

.. tabs::
 .. code-tab:: gdscript GDScript

    @onready var netw := Netw.of(self)

    func _on_participant_joined(participant: NetwParticipant) -> void:
        netw.get_service(BomberGamestate).register_player(participant)

The one rule for user code: configure in :godot:`_init() <Object#class_object_private_method__init>`,
talk to the session from the tree.
