.. _doc_manual_interest_management:

Interest management
===================

Once your project has more than one level, or more than one role for a peer,
"every client sees everything" stops being adequate. Some peers should not
receive certain entities at all. Some entities should appear and disappear
as the game's rules change. Networked answers this with the *interest
management system*: a small set of nodes and facades that decide, per peer
and per entity, who is allowed to see what.

Two ideas anchor the whole system. The first is the
:ref:`NetwInterestLayer <class_NetwInterestLayer>`: a named slice of
"who can see whom" maintained on the server. The second is the
:ref:`InterestGate <class_InterestGate>`: a node that ties a layer's state
to a piece of the scene tree so Godot's replication can act on it. Most of
this page is about how those two compose, what the public API on each one
is for, and how the same primitives become area-of-interest filtering,
stealth, or combat scenes depending on who calls which method when.

The mental model
----------------

An interest layer is three things:

- A **viewer set**: the peer ids participating in this layer. The server
  maintains it through :ref:`add_viewer() <class_NetwInterestLayer_method_add_viewer>`
  and :ref:`remove_viewer() <class_NetwInterestLayer_method_remove_viewer>`.
- An **entity set**: the entities the layer controls visibility of.
- A **policy** that combines the two:
  :ref:`HIDE_FROM_OUTSIDERS <class_NetwInterestLayer_constant_HIDE_FROM_OUTSIDERS>`
  (viewers see the entities, outsiders do not) or
  :ref:`HIDE_FROM_INSIDERS <class_NetwInterestLayer_constant_HIDE_FROM_INSIDERS>`
  (the inverse). The default is hide-from-outsiders, which is what almost
  every gameplay layer wants.

From those three values the server derives a per-(peer, entity) visibility.
A peer that satisfies the visibility for an entity receives that entity's
:godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` traffic. A peer
that does not, does not. An entity can participate in any number of
layers, if any layer admits a peer to that entity, the peer sees it.

Layers do not exist in the tree. They are pure state living inside the
:ref:`NetwInterest <class_NetwInterest>` facade exposed at
:ref:`MultiplayerTree.interest <class_MultiplayerTree_property_interest>`,
keyed by :godot:`StringName <StringName>`. You ask for a layer by id and
get one back; the system creates it on first access.

.. tabs::
 .. code-tab:: gdscript GDScript

    var sight := Netw.ctx(self).interest.layer(&"sight")
    sight.add_viewer(observer_peer_id)


Bound and unbound layers
------------------------

The single most important distinction in the system is whether a layer has
an :ref:`InterestGate <class_InterestGate>` attached to it or not.

A **bound** layer has an
:ref:`InterestGate <class_InterestGate>` node placed inside a subtree the
layer governs. The gate replicates the layer's viewers and policy to
admitted clients through Godot's spawn-sync, and (because the gate is a
:godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` whose visibility
filter follows the layer's verdict) the engine's
:godot:`MultiplayerSpawner <MultiplayerSpawner>` spawns or despawns the
gate's parent subtree per peer. The gate is also where the **public
membership API** lives:
:ref:`track_entity() <class_InterestGate_method_track_entity>` and
:ref:`untrack_entity() <class_InterestGate_method_untrack_entity>`.

An **unbound** layer has no gate. It influences the wire only by changing
each entity's synchronizer visibility. Membership and viewers are entirely
server-side. Clients receive transition signals for unbound layers through
a lightweight server-driven relay; they do not see the layer's viewer or
entity sets directly.

The rule of thumb:

- If the workflow is *"this subtree should spawn for some peers and not for
  others"*, use a **bound** layer with a gate.
- If the workflow is *"this entity has an extra tag that affects who
  receives its synchronizer"*, use an **unbound** layer and let the
  server's admission engine do the rest.

The gate as the public bound-layer API
--------------------------------------

When you reach for a bound layer, you do not call
:ref:`add_entity() <class_NetwInterestLayer_method_add_entity>` on the
layer yourself, you call
:ref:`track_entity() <class_InterestGate_method_track_entity>` on the
gate. The gate then does the right thing on each side: it registers the
entity with the layer on the server, and it admits the entity to the
local client mirror on every admitted peer.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Server-side: bring this entity into the combat scene's layer.
    combat_scene.gate.track_entity(entity)

    # Server-side later: combat ended.
    combat_scene.gate.untrack_entity(entity)

The pair has two important properties.

First, the calls are **idempotent**. Tracking an entity twice is a no-op;
untracking one that isn't tracked is too. Lifecycle owners can call them
from any reasonable place without coordinating with each other.

Second, the calls are **explicit**. Bound-layer membership is not
discovered from the tree, derived from tags on the entity, or inferred from
ancestor relationships. Whoever owns the workflow's lifecycle, the
:ref:`MultiplayerScene <class_MultiplayerScene>` for a level, your combat
orchestrator for a fight, your AoI system for a proximity bucket, makes
the call when the entity should join, and the matching call when it should
leave. The gate trusts the caller. This is what makes the same API serve
"player walks into the level" and "player walks into combat" with no
ceremony in the gate itself.

.. note::

   The structural relationship between the entity and the gate's subtree
   is the caller's responsibility, not the gate's. A combat scene gate
   admits any entity its orchestrator hands to it, the participants
   don't have to be structurally inside the combat scene's subtree on the
   server. What the gate guarantees is that the entity will be tracked on
   every peer the gate admits, and untracked on every peer it doesn't.

Listening for transitions
-------------------------

Three signal pairs cover the three different questions you might want to
ask. They look adjacent on paper but answer genuinely different things.
Pick by the question, not by the layer.

**"Did this peer just become able to see this entity?"** - server-side
admission. Use
:ref:`interest_enter <class_NetwInterestLayer_signal_interest_enter>` /
:ref:`interest_exit <class_NetwInterestLayer_signal_interest_exit>` on the
layer, or the per-entity rebroadcast at
:ref:`NetwEntity.interest_enter <class_NetwEntity>` /
:ref:`interest_exit <class_NetwEntity>`. These fire on the server only,
once per (entity, peer) visibility change.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Server: react when a peer gains admission to an entity through a layer.
    var sight := Netw.ctx(self).interest.layer(&"sight")
    sight.interest_enter.connect(func(entity, peer_id):
        analytics.peer_saw(peer_id, entity.entity_id)
    )

**"Did I just gain or lose sight of this entity through this layer?"** -
client-side local view. Use
:ref:`entity_visible <class_NetwInterestLayer_signal_entity_visible>` /
:ref:`entity_hidden <class_NetwInterestLayer_signal_entity_hidden>` on the
layer. Works uniformly for bound and unbound layers, the caller does not
need to know which transport delivered the transition.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Client: react when an entity becomes locally visible on a layer.
    var sight := Netw.ctx(self).interest.layer(&"sight")
    sight.entity_visible.connect(func(entity):
        add_marker(entity.owner)
    )
    sight.entity_hidden.connect(func(entity):
        remove_marker(entity.owner)
    )

**"Did someone else just gain or lose visibility of me?"** -- owner-side
awareness. Used for HUD elements such as "you are being watched" or
"these peers can see your stealth indicator." Enable
:ref:`report_observers <class_InterestComponent_property_report_observers>`
on the entity's
:ref:`InterestComponent <class_InterestComponent>` and connect to
:ref:`NetwEntity.observer_entered <class_NetwEntity>` /
:ref:`observer_left <class_NetwEntity>` on the owner client.

These three pairs are computed from three different sources of truth.
:ref:`interest_enter <class_NetwInterestLayer_signal_interest_enter>` and :ref:`interest_exit <class_NetwInterestLayer_signal_interest_exit>` come from the server's admission
engine, they reflect what the server has decided, regardless of network
delivery. :ref:`entity_visible <class_NetwInterestLayer_signal_entity_visible>` and :ref:`entity_hidden <class_NetwInterestLayer_signal_entity_hidden>` reflect what is
currently spawned and admitted on the local client. :ref:`observer_entered <class_NetwEntity>`
and :ref:`observer_left <class_NetwEntity>` are a server-relayed signal aimed specifically at
the entity's owning peer.

They agree in the common case and may diverge by design at edges, a
listen-server host always has every entity replicated to its own process,
for instance, so
:ref:`entity_visible <class_NetwInterestLayer_signal_entity_visible>` will
fire on the host for entities the host's character is not supposed to
perceive. That is the right answer to the question "is this entity
replicated to me on this layer," even though it is not the right answer to
"should my character react to this entity." Perception, when you need it,
is a separate concern with separate signals that read from the interest
layer to make a local presentation decision.

Worked example: area-of-interest filtering
------------------------------------------

A proximity AoI region is one bound layer per region, viewers updated by
whatever proximity system you write, entities tracked as they enter and
leave range.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Region root has an InterestGate child with layer_id = &"aoi:zone_a".
    var gate: InterestGate = $AoIZoneA/InterestGate
    var layer := gate._layer  # or Netw.ctx(self).interest.layer(&"aoi:zone_a")

    # Proximity system tick:
    for peer_id in peers_in_range_of_zone_a():
        layer.add_viewer(peer_id)
    for peer_id in peers_who_left_zone_a():
        layer.remove_viewer(peer_id)

    # Entity lifecycle:
    func _on_entity_entered_zone_a(entity: NetwEntity) -> void:
        gate.track_entity(entity)

    func _on_entity_left_zone_a(entity: NetwEntity) -> void:
        gate.untrack_entity(entity)

Clients in range receive the region's contents through Godot's spawn-sync;
clients out of range do not. The
:ref:`entity_visible <class_NetwInterestLayer_signal_entity_visible>` and
:ref:`entity_hidden <class_NetwInterestLayer_signal_entity_hidden>` signals
on the AoI layer fire automatically as entities cross the region boundary,
so client-side HUD code (minimap markers, audio buses) can react without
polling.

Worked example: stealth
-----------------------

A stealthed entity uses a bound layer to restrict which peers receive its
replication at all. The layer is keyed by the entity (so each stealthed
entity has its own visibility rules) or by a team/role.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Per-entity stealth gate placed on the entity's scene root.
    var gate: InterestGate = stealth_root.get_node("InterestGate")
    var layer := Netw.ctx(self).interest.layer(&"stealth:%d" % entity.peer_id)

    # Server-side gameplay rule: detect-stealth proc applies.
    if has_detect_stealth(observer_peer):
        layer.add_viewer(observer_peer)

    # Entity self-enrolls so the gate controls its replication.
    gate.track_entity(entity)

Peers admitted by the layer receive the entity; everyone else gets
nothing. The structural fact that drives spawn/despawn is the gate's own
visibility, which the layer's verdict controls.

For the listen-server host case, the server process must hold the
entity's node, but the host's *character* should not perceive a stealthed
opponent, the interest layer cannot help you. That is by design: the
layer's job is replication. A local presentation filter that reads from
the layer's viewer set and toggles ``visible`` or ``process_mode`` on the
host's machine is the right tool, and it stays out of the IMS entirely.

Worked example: combat scenes
-----------------------------

A combat scene is the same primitive as a level scene: a node carrying a
gate at its root, instantiated on the server, with viewers managed by the
combat orchestrator and entities tracked as participants arrive.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Server-side combat orchestrator.
    var combat := preload("res://gameplay/CombatScene.tscn").instantiate()
    combat.gate.layer_id = &"combat:%d" % combat_id
    add_child(combat)

    for player in participants:
        combat.gate.add_viewer(player.peer_id)
        combat.gate.track_entity(player.entity)

    # ...combat runs, participants take actions...

    # On end:
    for player in participants:
        combat.gate.untrack_entity(player.entity)
    combat.queue_free()

Spectators are peers added as viewers without being tracked as
entities, they receive the combat scene but do not participate in its
layer's entity set. Non-participant peers receive nothing and have no idea
the scene exists.

This is the same shape as :ref:`MultiplayerScene <class_MultiplayerScene>`
uses for levels. The container differs (a level vs. a combat instance),
the layer id differs, the lifecycle owner differs, but the gate API is
identical.

What the interest system is not
-------------------------------

A few responsibilities live next to the IMS without being part of it:

- **Gameplay rules**. The IMS asks the server's
  :ref:`viewers <class_NetwInterestLayer_property_viewers>` and
  :ref:`policy <class_NetwInterestLayer_property_policy>` for a verdict;
  it does not decide who *should* be a viewer. Add and remove viewers
  from your own systems -- AoI ticks, line-of-sight checks, party
  membership, scripted reveal events.
- **Local perception**. Whether the local character can hear, see, or
  target an entity that is replicated to its peer is gameplay. The IMS
  determines what arrives over the wire; perception is what the local
  process does with it. Build perception as a small local node that reads
  from a gate's viewer set, not as a parallel admission system.
- **Continuous replication**. The IMS controls *whether* an entity's
  synchronizers send to a peer. It does not control how often or what
  delta strategy each synchronizer uses. Those remain configuration on
  the :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` nodes.