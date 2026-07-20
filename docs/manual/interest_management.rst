.. _doc_manual_interest_management:

Interest management
===================

Interest management decides which participant should see each networked
entity. The server owns the decision. A
:ref:`NetwInterestLayer <class_NetwInterestLayer>` supplies a viewer set, an
entity set, and a composition policy. The
:ref:`NetwInterestInterface <class_NetwInterestInterface>` combines every
layer, native synchronizer visibility, and entity ancestry into one committed
per-peer matrix.

The matrix has two consumers with deliberately different questions:

- ``wire_admits(peer_id, entity)`` asks whether replication may send the
  entity. Server authority always knows every entity.
- ``participant_sees(peer_id, entity)`` asks whether that participant's
  committed row admits the entity. The listen host is evaluated like every
  other player.

Gameplay normally calls
``NetwEntity.of(node).interest.is_visible_to(peer_id)``. Replication code uses
the wire query.

The layer model
---------------

A layer contains:

- ``viewers``: peer ids maintained with
  :ref:`add_viewer() <class_NetwInterestLayer_method_add_viewer>` and
  :ref:`remove_viewer() <class_NetwInterestLayer_method_remove_viewer>`.
- ``entities``: entity records maintained with
  :ref:`add_entity() <class_NetwInterestLayer_method_add_entity>` and
  :ref:`remove_entity() <class_NetwInterestLayer_method_remove_entity>`.
- ``policy``:
  :ref:`HIDE_FROM_OUTSIDERS <class_NetwInterestLayer_constant_HIDE_FROM_OUTSIDERS>`
  admits viewers, while
  :ref:`HIDE_FROM_INSIDERS <class_NetwInterestLayer_constant_HIDE_FROM_INSIDERS>`
  admits everyone except viewers.

Membership in several layers is an OR. Any admitting layer grants the peer's
row. Entity ancestry is an AND: a denied parent clamps every descendant. A
native :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` visibility
decision is folded into that same row, so spawn and continuous state use one
answer.

Mutations commit together at the deferred end-of-frame flush. Queries keep
reading the previous committed row until then. Use ``flush_now()`` in tests or
before an in-frame spawn that must observe a new admission immediately.

.. tabs::
 .. code-tab:: gdscript GDScript

    var sight := Netw.of(self).interest.layer(&"sight")
    sight.add_entity(target_entity)
    sight.add_viewer(observer_peer_id)
    Netw.of(self).interest.flush_now()

Declaring entity interest
--------------------------

Every :ref:`NetwEntity <class_NetwEntity>` owns one stable ``interest`` handle.
Declare memberships from ``_init()`` so every peer constructs the same layer
labels and local callbacks. Only server authority mutates the authoritative
entity sets.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        Netw.configure_interest(self) \
            .layer(&"team:red") \
            .layer(&"nearby") \
            .on_enter(_on_interest_enter) \
            .on_leave(_on_interest_leave)

    func _on_interest_enter(layer_id: StringName, peer_id: int) -> void:
        pass

    func _on_interest_leave(layer_id: StringName, peer_id: int) -> void:
        pass

Runtime systems may call ``entity.interest.join(layer_id)`` and
``entity.interest.leave(layer_id)`` directly. Declarations survive tree exits
and reattach when the entity enters another session.

Scene admission and ancestry
----------------------------

Each :ref:`MultiplayerScene <class_MultiplayerScene>` wrapper is an ordinary
entity in its ``scene:<name>`` layer. Every descendant entity joins that scene
layer when the spawn pipeline captures its parent anchor. Reparenting updates
the old and new scene memberships automatically.

The wrapper's committed row therefore clamps its whole subtree. Calling
``scene.connect_peer(peer_id)`` adds a viewer to the scene layer. Calling
``scene.disconnect_peer(peer_id)`` removes it. There is no scene gate node and
no replicated viewer list.

For an entity outside a managed scene, add it to a gameplay layer directly or
declare the layer through ``Netw.configure_interest``.

Wire leave policy
-----------------

When the aggregate row changes from admitted to denied, the wire leave policy
decides what happens to an already materialized client node:

- ``DESPAWN`` frees the peer's node. This is the default.
- ``RETAIN`` keeps the same node with its last received state. Continuous
  state resumes when admission returns.
- ``CUSTOM`` retains the node and calls the configured server callback with
  ``(peer_id, layer_id)``.

A layer supplies ``default_leave_policy``. An entity override takes priority.
An ancestor despawn still dominates a descendant retain policy because the
descendant cannot exist without its replicated parent.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        Netw.configure_interest(self).layer(
            &"stealth",
            NetwInterestInterface.LeavePolicy.RETAIN,
        )

    # CUSTOM structurally requires its callback.
    entity.interest.on_leave_policy(
        &"stealth",
        NetwInterestInterface.LeavePolicy.CUSTOM,
        _on_wire_leave,
    )

Local perception policy
-----------------------

The listen-server process must retain every authoritative entity, but the
host's player should perceive only its honest participant row. A retained
client has the same presentation problem. ``PerceptionPolicy`` applies one
local solution to both cases:

- ``HIDE`` snapshots every :godot:`CanvasItem <CanvasItem>` and
  :godot:`Node3D <Node3D>` visibility value in the entity subtree, hides them,
  and mutes its audio players. Admission restores the exact snapshot.
- ``SHOW`` leaves presentation unchanged.
- ``CUSTOM`` calls ``(visible, peer_id, layer_id)`` on local leave and enter.

``HIDE`` never changes ``process_mode``, physics, or simulation authority. Use
``CUSTOM`` for collision masks, minimap markers, fades, and other game-specific
presentation.

The layer default is ``HIDE``. Set ``default_perception_policy`` during
symmetric setup on every peer when changing that default, because viewer sets
and policies are server-private and are not replicated. A per-entity override
declared from ``_init()`` is naturally symmetric:

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        Netw.configure_interest(self).layer(
            &"stealth",
            NetwInterestInterface.LeavePolicy.RETAIN,
            NetwInterestInterface.PerceptionPolicy.SHOW,
        )

    func configure_fade() -> void:
        NetwEntity.of(self).interest.on_perception_policy(
            &"stealth",
            NetwInterestInterface.PerceptionPolicy.CUSTOM,
            _on_perception,
        )

    func _on_perception(
        visible: bool,
        _peer_id: int,
        _layer_id: StringName,
    ) -> void:
        fade_to(1.0 if visible else 0.2)

Transition surfaces
-------------------

Use the signal pair that answers your question:

- Server admission: ``NetwInterestLayer.interest_enter`` and
  ``interest_exit`` receive ``(entity, peer_id)``.
- Local client attribution: ``entity_visible`` and ``entity_hidden`` receive
  the entity for the local participant's layer edge.
- Entity callbacks: ``InterestHandle.on_enter`` and ``on_leave`` receive
  ``(layer_id, peer_id)``.
- Owner awareness: ``InterestHandle.on_observed`` and ``on_unobserved`` receive
  another observer's peer id.

Clients learn only layer attribution for their own committed row and optional
owner-awareness edges. They never receive another participant's row, viewer
sets, or server policy inputs.

Area-of-interest example
------------------------

An area system owns one layer per region. It updates viewers and membership;
the interest engine handles spawn, state, and local transitions.

.. tabs::
 .. code-tab:: gdscript GDScript

    var zone := Netw.of(self).interest.layer(&"aoi:zone_a")

    func update_zone() -> void:
        for peer_id in peers_entering_zone_a():
            zone.add_viewer(peer_id)
        for peer_id in peers_leaving_zone_a():
            zone.remove_viewer(peer_id)

    func entity_entered(entity: NetwEntity) -> void:
        zone.add_entity(entity)

    func entity_left(entity: NetwEntity) -> void:
        zone.remove_entity(entity)

Stealth example
---------------

Use one layer per stealth group or target. Detection grants viewer membership.
The honest host row and retained-client projection use the same perception
configuration.

.. tabs::
 .. code-tab:: gdscript GDScript

    var stealth := Netw.of(self).interest.layer(&"stealth:red")
    stealth.default_leave_policy = \
        NetwInterestInterface.LeavePolicy.RETAIN

    if observer_detects_target(observer_peer_id, target):
        stealth.add_viewer(observer_peer_id)
    else:
        stealth.remove_viewer(observer_peer_id)

Debugging checklist
-------------------

When visibility is surprising:

1. Call ``layer.debug_dump(peer_id)`` to inspect the layer verdict.
2. Call ``entity.interest.layer_ids()`` to check declared local labels.
3. Compare ``participant_sees`` with ``wire_admits`` for a listen host.
4. Confirm the mutation has reached ``flush()`` or call ``flush_now()`` in a
   focused test.
5. Check the parent entity. A denied ancestor always clamps the child.

Interest decides whether a state stream may exist. Synchronizer frequency,
quantization, and property selection remain replication configuration.
