.. _doc_manual_scenes_and_players:

Scenes and players
==================

Networked separates the "world" from the "players in it". The
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` decides which
levels exist on which peer and when, while the
:ref:`MultiplayerEntity <class_MultiplayerEntity>` decides which actors enter
those levels and on whose authority. This page works through both, with the
:ref:`MultiplayerScene <class_MultiplayerScene>` container in the middle as
the glue.

Levels, scenes, and the scene manager
-------------------------------------

A *level* is a normal Godot :godot:`PackedScene <PackedScene>`: a tree of
nodes saved on disk. A *scene*, in Networked terminology, is one running
instance of a level inside a session, wrapped in a
:ref:`MultiplayerScene <class_MultiplayerScene>` so its lifetime, visibility
filters, and spawn signals are controlled centrally. The
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` keeps the
running scenes in its :ref:`active_scenes <class_MultiplayerSceneManager_property_active_scenes>` dictionary, keyed by node name, and
owns the :godot:`MultiplayerSpawner <MultiplayerSpawner>` that replicates
new scenes to clients.

The manager supports two complementary controls per level:

- **Load mode**: :ref:`ON_STARTUP <class_MultiplayerSceneManager_constant_ON_STARTUP>` spawns the level the moment the server
  finishes hosting. :ref:`ON_DEMAND <class_MultiplayerSceneManager_constant_ON_DEMAND>` waits until a player explicitly asks for
  the level via :ref:`activate_scene() <class_MultiplayerSceneManager_method_activate_scene>`.
- **Empty action**: when the last player leaves a scene, the manager can
  :ref:`KEEP_ACTIVE <class_MultiplayerSceneManager_constant_KEEP_ACTIVE>` (the default for lobbies), :ref:`FREEZE <class_MultiplayerSceneManager_constant_FREEZE>` (pause the level so
  it stops processing but stays cheap to wake up), or :ref:`DESTROY <class_MultiplayerSceneManager_constant_DESTROY>` (free the
  scene so memory is reclaimed). The right choice depends on whether you
  want late joiners to find the level instantly or whether the level is
  expensive to keep alive.

For a single-scene project, you do not need to think about any of this.
Dropping a scene with a :ref:`MultiplayerEntity <class_MultiplayerEntity>`
descendant directly under the tree makes Networked auto-configure a
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` with that scene as its only spawnable, in
:ref:`ON_STARTUP <class_MultiplayerSceneManager_constant_ON_STARTUP>` mode. The first time you need a second level (a lobby plus a
match, say) you'll add the manager explicitly and configure both there.

The MultiplayerScene container
------------------------------

When a level spawns, the manager wraps it in a
:ref:`MultiplayerScene <class_MultiplayerScene>` and parents the actual
level node underneath. The container does three useful things:

1. It hooks every :godot:`MultiplayerSpawner <MultiplayerSpawner>` in the
   level into the scene's :ref:`InterestGate <class_InterestGate>`
   so per-peer visibility filters apply automatically. You get visibility
   filtering for free without touching the engine API.
2. It tracks the players currently inside the scene, emitting signals
   as they arrive or leave.
3. It provides readiness gates (via :ref:`NetwScene <class_NetwScene>`) so
   the game only starts once every player has finished loading.

You do not instantiate :ref:`MultiplayerScene <class_MultiplayerScene>`
yourself. The scene manager creates them, and the wrapper does its work
through the synchronizer and the
:godot:`MultiplayerSpawner <MultiplayerSpawner>` you already configured
on the level.

The MultiplayerEntity
--------------------

The :ref:`MultiplayerEntity <class_MultiplayerEntity>` is the one piece of
the addon every gameplay scene touches. It extends
:godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` so it can be
authored visually in the *Replication* panel, but the runtime treats every
configured property as **spawn-only**: the only thing the component
guarantees is that the property's value is present on the client when the
entity enters the tree. Continuous replication is the job of additional
sibling :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` nodes
that you configure for that purpose.

This separation is deliberate. The spawn snapshot needs to be small,
strictly server-driven, and decoded before the node's
:godot:`_ready() <Node#class_node_private_method__ready>` runs. Ongoing replication
has different traffic patterns and different authority rules. Sharing one
node for both jobs leads to spawn packets that secretly drift over time --
exactly the kind of bug "I added it in the inspector and it worked" has
trouble surviving.

Authority modes
~~~~~~~~~~~~~~~

The component's :ref:`AuthorityMode <class_MultiplayerEntity_property_authority_mode>`
controls who is in charge of the entity's :godot:`owner <Node#class_node_property_owner>` node:

- :ref:`SERVER <class_MultiplayerEntity_constant_SERVER>`: the server peer (id 1) is the multiplayer authority. Use
  this for NPCs, level props, and anything that should remain
  server-authoritative.
- :ref:`CLIENT <class_MultiplayerEntity_constant_CLIENT>`: the represented peer (parsed from the entity's name in the
  form ``entity_id|peer_id``) is the multiplayer authority. This is the
  setting for player avatars where the owning client reads input and the
  server only validates.

Regardless of the owner's authority, the *synchronizer itself* always sits
on the server. That asymmetry is what lets the server issue spawn and
despawn commands for client-authoritative entities without playing
permission games. It owns the synchronizer, the synchronizer owns the
spawn list, and the entity rides along.

Contributing spawn properties
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sibling components can extend the spawn packet without touching the
spawner's exported config. From the
:godot:`NOTIFICATION_PARENTED <Node#class_node_constant_notification_parented>`
hook of the sibling, call
:ref:`contribute_spawn_property() <class_NetwEntity>` with the source node
and property the synchronizer should bundle:

.. tabs::
 .. code-tab:: gdscript GDScript

    func _notification(what: int) -> void:
        if what == NOTIFICATION_PARENTED:
            var entity := Netw.ctx(self).entity
            entity.contribute_spawn_property(self, &"health")
            entity.spawning.connect(_on_spawning)

    func _on_spawning() -> void:
        if multiplayer.is_server():
            hydrate_from_db()

The ordering is important: contributions must happen in
:godot:`NOTIFICATION_PARENTED <Node#class_node_constant_notification_parented>`, because Godot reads the synchronizer's
replication config between scene instantiation and tree entry. Connecting
to :ref:`spawning <class_MultiplayerEntity_signal_spawning>` and adding
properties from inside it is too late. The spawn packet has already been
serialized.

.. warning::

    Do not write to spawn properties from clients during :godot:`_ready() <Node#class_node_private_method__ready>`. The
    spawn snapshot has just landed and your write will race the next
    on-change synchronizer tick. Wait for the
    :ref:`spawned <class_NetwEntity>` signal if you need to touch the
    initial state from sibling components.

Spawning and despawning
~~~~~~~~~~~~~~~~~~~~~~~

Most spawns happen inside the addon: a client connects, the server
resolves their :ref:`ResolvedJoin <class_ResolvedJoin>`, and
:ref:`spawn_player() <class_MultiplayerEntity_method_spawn_player>` drops a
copy of the template into the target
:ref:`MultiplayerScene <class_MultiplayerScene>`. For everything else (NPCs, projectiles, loot) there are two helpers:

- :ref:`spawn_under() <class_MultiplayerEntity_method_spawn_under>`: the
  simple case: clone the template under a parent and give it an entity id.
- :ref:`instantiate_from() <class_MultiplayerEntity_method_instantiate_from>`:
  the configurable case: clone the template, run a callback on the copy
  before it enters the tree, and let the caller add it to the scene.

Both are server-only. The copy goes through the same spawn lifecycle as a
player would: it picks up the spawn snapshot, runs the
:ref:`spawning <class_MultiplayerEntity_signal_spawning>` signal so sibling
components can hydrate, registers with the scene's synchronizer, and
finally fires :ref:`spawned <class_NetwEntity>`.

Despawning is symmetric:
:ref:`despawn() <class_MultiplayerEntity_method_despawn>` flushes the
:ref:`SaveComponent <class_SaveComponent>` (unless you ask it not to),
forces authority back to the server so visibility updates settle cleanly,
and frees the owner. The reason string you pass through
:ref:`DespawnOpts <class_DespawnOpts>` shows up in logs and in the
:ref:`despawning <class_MultiplayerEntity_signal_despawning>` signal, so
custom systems (achievements, death cams, kill feeds) can pivot on it
without parsing strings out of the engine.

A complete player template
~~~~~~~~~~~~~~~~~~~~~~~~~~

Putting the pieces together, a minimal client-authoritative player scene
contains:

- A :godot:`CharacterBody2D <CharacterBody2D>` (or 3D equivalent) with the
  movement script.
- A :ref:`MultiplayerEntity <class_MultiplayerEntity>` with :ref:`AuthorityMode <class_MultiplayerEntity_property_authority_mode>`
  set to :ref:`CLIENT <class_MultiplayerEntity_constant_CLIENT>` and the body's position listed as a spawn property.
- A sibling :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` for
  continuous state (position, animation frame, weapon held).
- Optionally, a :ref:`SaveComponent <class_SaveComponent>` so the player's
  data persists across reconnects, and a
  :ref:`MultiplayerInterpolator <class_MultiplayerInterpolator>` on remote copies to
  smooth out the snapshotted position between server ticks.

The ``examples/quick_start/Player.tscn`` and
``examples/bomber/game/player.tscn`` scenes in the repository both follow
this shape and are good reference reads.
