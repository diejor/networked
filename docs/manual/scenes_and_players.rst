.. _doc_manual_scenes_and_players:

Scenes and players
==================

Networked separates the "world" from the "players in it". The
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` decides which
levels exist on which peer and when, while :ref:`NetwEntity <class_NetwEntity>`
decides which actors enter those levels and on whose authority. This page
works through both, with the
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

A single-scene project still needs an explicit
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` child under the
tree, configured with the level as its only spawnable scene in
:ref:`ON_STARTUP <class_MultiplayerSceneManager_constant_ON_STARTUP>` mode.
The first time you need a second level (a lobby plus a match, say) you add
its scene config alongside the first.

The MultiplayerScene container
------------------------------

When a level spawns, the manager wraps it in a
:ref:`MultiplayerScene <class_MultiplayerScene>` and parents the actual
level node underneath. The container does three useful things:

1. It enrolls the wrapper and descendant entity roots in the scene's
   :ref:`NetwInterestLayer <class_NetwInterestLayer>`. The wrapper row clamps
   the subtree, so per-peer visibility applies automatically.
2. It tracks the players currently inside the scene, emitting signals
   as they arrive or leave.
3. It provides readiness gates (via
   :ref:`MultiplayerScene.create_readiness_gate() <class_MultiplayerScene_method_create_readiness_gate>`)
   so the game only starts once every player has finished loading.

You do not instantiate :ref:`MultiplayerScene <class_MultiplayerScene>`
yourself. The scene manager creates them, and the wrapper does its work
through the synchronizer and the
:godot:`MultiplayerSpawner <MultiplayerSpawner>` you already configured
on the level.

The entity record
------------------

:ref:`NetwEntity <class_NetwEntity>` is the identity, control, and spawn
record every networked actor carries. It is not a node: it attaches itself as
metadata on the entity root the first time any sibling script resolves it, so
it adds no extra node to your scene tree. The runtime treats a marked
property as **spawn-only**: the only thing a
:ref:`on_spawn() <class_NetwScriptModel>` mark guarantees is that the
property's value is present on the client when the entity enters the tree.
Continuous replication is the job of sibling
:godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` nodes or
``.state()``/``.input()``/``.broadcast()`` marks that you configure for that
purpose.

This separation is deliberate. The spawn snapshot needs to be small,
strictly server-driven, and decoded before the node's
:godot:`_ready() <Node#class_node_private_method__ready>` runs. Ongoing replication
has different traffic patterns and different authority rules. Sharing one
channel for both jobs leads to spawn packets that secretly drift over time --
exactly the kind of bug "I added it in the inspector and it worked" has
trouble surviving.

Authority modes
~~~~~~~~~~~~~~~

:ref:`NetwEntity.initial_controller <class_NetwEntity>`
controls who is in charge of the entity's :godot:`owner <Node#class_node_property_owner>` node at spawn:

- ``SERVER``: the server peer (id 1) is the multiplayer authority. Use
  this for NPCs, level props, and anything that should remain
  server-authoritative.
- ``REPRESENTED_PEER``: the represented peer (parsed from the entity's name in the
  form ``entity_id|peer_id``) is the multiplayer authority. This is the
  setting for player avatars where the owning client reads input and the
  server only validates.

Set it from the entity root's own ``_init()``:

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        var entity := NetwEntity.resolve(self)
        entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER

The server always issues spawn and despawn commands over the addon's own
transport, regardless of which peer controls the owner node. That is what
lets the server manage client-authoritative entities without playing
permission games.

Spawn lifecycle signals
~~~~~~~~~~~~~~~~~~~~~~~

Sibling components that only need decoded identity can connect to
:ref:`NetwEntity.spawning <class_NetwEntity>` or
:ref:`NetwEntity.spawned <class_NetwEntity>` in code.
``spawning`` runs once after identity, authority, and spawn properties are
applied. ``spawned`` runs once after scene registration and the owner
finishes :godot:`_ready() <Node#class_node_private_method__ready>`.

Use
:godot:`NOTIFICATION_PARENTED <Node#class_node_constant_notification_parented>`
to mark a property so its value rides the spawn packet. From that hook,
call :ref:`Netw.configure_property() <class_Networked>` on the property and
chain ``.on_spawn()``:

.. tabs::
 .. code-tab:: gdscript GDScript

    func _notification(what: int) -> void:
        if what == NOTIFICATION_PARENTED:
            Netw.configure_property(self, &"health").on_spawn()
            NetwEntity.resolve(self).spawning.connect(_on_spawning)

    func _on_spawning() -> void:
        if multiplayer.is_server():
            hydrate_from_db()

The ordering is important: the mark still must happen in
:godot:`NOTIFICATION_PARENTED <Node#class_node_constant_notification_parented>`, because the spawn pipeline reads marked
spawn state between scene instantiation and tree entry. Connecting
to :ref:`spawning <class_NetwEntity>` and marking properties from inside it
is too late. The spawn packet has already been encoded.

.. warning::

    Do not write to spawn properties from clients during :godot:`_ready() <Node#class_node_private_method__ready>`. The
    spawn snapshot has just landed and your write will race the next
    on-change synchronizer tick. Wait for the
    :ref:`spawned <class_NetwEntity>` signal if you need to touch the
    initial state from sibling components.

Spawning and despawning
~~~~~~~~~~~~~~~~~~~~~~~

Most spawns happen inside the addon: a client connects, the server
accepts their :ref:`NetwParticipant <class_NetwParticipant>`, and
:ref:`NetwEntity.instantiate_player() <class_NetwEntity>` drops a
copy of the template into the target
:ref:`MultiplayerScene <class_MultiplayerScene>`. For everything else (NPCs, projectiles, loot) there are two helpers:

- :ref:`NetwEntity.spawn_under() <class_NetwEntity>`: the
  simple case: clone the template under a parent and give it an entity id.
- :ref:`NetwEntity.instantiate_from() <class_NetwEntity>`:
  the configurable case: clone the template, run a callback on the copy's
  entity record before it enters the tree, and let the caller add it to the
  scene.

Both are server-only. The copy goes through the same spawn lifecycle as a
player would: it picks up the spawn snapshot, runs
:ref:`spawning <class_NetwEntity>`, registers with
the scene's synchronizer, finishes :godot:`_ready() <Node#class_node_private_method__ready>`,
and finally fires :ref:`spawned <class_NetwEntity>`.

Despawning is symmetric:
:ref:`NetwEntity.despawn() <class_NetwEntity>` flushes
:ref:`NetwPersistenceInterface <class_NetwPersistenceInterface>` state (unless you ask it not to),
forces authority back to the server so visibility updates settle cleanly,
and frees the owner. The reason string you pass through
``NetwEntity.DespawnOpts`` shows up in logs and in the
:ref:`despawning <class_NetwEntity>` signal, so
custom systems (achievements, death cams, kill feeds) can pivot on it
without parsing strings out of the engine.

A complete player template
~~~~~~~~~~~~~~~~~~~~~~~~~~

Putting the pieces together, a minimal client-authoritative player scene
contains:

- A :godot:`CharacterBody2D <CharacterBody2D>` (or 3D equivalent) with the
  movement script, whose ``_init()`` resolves the entity and sets
  ``initial_controller`` to ``REPRESENTED_PEER``, and marks the body's
  position with ``.on_spawn()``.
- A sibling :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` for
  continuous state (position, animation frame, weapon held).
- Optionally, :ref:`Netw.configure_persistence() <class_Networked>` so the
  player's data persists across reconnects, and a
  :ref:`MultiplayerInterpolator <class_MultiplayerInterpolator>` on remote copies to
  smooth out the snapshotted position between server ticks.

The ``examples/quick_start/Player.tscn`` and
``examples/bomber/game/player.tscn`` scenes in the repository both follow
this shape and are good reference reads.
