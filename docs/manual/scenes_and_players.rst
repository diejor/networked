.. _doc_manual_scenes_and_players:

Scenes and players
==================

Networked separates the "world" from the "players in it". A *scene* owns an
admission boundary and decides which peers may see what is inside it, while
:ref:`NetwEntity <class_NetwEntity>` decides which actors enter and on whose
authority. This page works through both.

Levels, scenes, and declarations
--------------------------------

A *level* is a normal Godot :godot:`PackedScene <PackedScene>`: a tree of nodes
saved on disk. A *scene* is one running instance of a level inside a session.

A scene is not a special node class. It is an ordinary entity that carries one
extra fact -- it declares itself a scene -- and everything inside its subtree
belongs to it by ancestry alone. Nothing enrolls, and nothing has to be told
when an entity moves:

.. tabs::
 .. code-tab:: gdscript GDScript

    # On the level root, before it goes live.
    func _init() -> void:
        Netw.configure_multiplayer_scene(self).labeled(&"Arena")

Because membership is ancestry, reparenting a node into a scene's subtree
*is* joining that scene. The one thing parenting does not carry is admission,
which is why moving a player across a boundary goes through a verb.

The stem is not an identity. Two live instances of one arena share the stem
``&"Arena"`` and own two separate admission boundaries, so identity is always
the scene's entity RID. :ref:`scene_find() <class_NetwMultiplayer_method_scene_find>`
answers "an instance of this stem";
:ref:`scene_find_all() <class_NetwMultiplayer_method_scene_find_all>` answers
"every instance".

A session always owns its scene registry, so declaring a scene never requires a
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` node. That node is
inspector sugar over the same declaration rows a tree-less session builds
directly.

Reaching a scene
----------------

Every entity resolves the scene it is in through
:ref:`NetwEntity.scene <class_NetwEntity_property_scene>`, which answers a
:ref:`NetwSceneHandle <class_NetwSceneHandle>`. The handle is a view over the
scene's entity, never ``null``, and one scene has exactly one handle -- so
``==`` answers "the same scene":

.. tabs::
 .. code-tab:: gdscript GDScript

    var scene := NetwEntity.of(self).scene
    if scene.is_declared:
        scene.admit(participant)
        for player in scene.players:
            greet(player)

    if NetwEntity.of(shooter).scene == NetwEntity.of(target).scene:
        apply_damage()

The handle reports the scene's edges as callbacks rather than signals, because
a callback names the scene rather than whichever node currently stands in for
it. Registrations chain:

.. tabs::
 .. code-tab:: gdscript GDScript

    NetwEntity.of(self).scene \
        .on_participant_entered(_greet) \
        .on_participant_left(_farewell) \
        .on_player_entered(_seat_car)

:ref:`players <class_NetwSceneHandle_property_players>` and
:ref:`entities <class_NetwSceneHandle_property_entities>` are derived from the
subtree rather than from a roster, so they cannot drift out of step with the
tree. A player is simply an entity that carries a peer.

The container node
------------------

A scene mounts under a container node, which the spawn recipe builds on every
peer. The container carries no script: which node class it is follows from the
scene's declared :ref:`SceneIsolation <enum_NetwMultiplayer_SceneIsolation>`.

.. code-block::

    SCENE_ISOLATION_NONE        every peer builds a plain Node
    SCENE_ISOLATION_OWN_WORLD   a hosting peer builds a SubViewport with its
                                own world, so two live scenes never share a
                                physics space; a peer that only views the
                                scene still builds a plain Node, because only
                                the host simulates

Isolation is settled before the entity arms, because the recipe runs on every
peer and a peer that built the wrong kind of container cannot anchor the
scene's children. Reach the container's content root as
:ref:`level <class_NetwSceneHandle_property_level>`; a scene with no content is
a pure admission boundary, which is useful for a lobby, a spectator scope, or a
team channel.

Match state on the scene itself
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A scene is an ordinary networked entity, so anything a match needs to
agree on — a countdown, a ready roster, a vote — is a replicated property
on the scene, not a bespoke API. This is the recipe to reach for, and it
generalizes: what works for a countdown works for map voting or vote-kick.

A countdown is one replicated integer. Store the *target tick* rather than
the seconds remaining, so late joiners get the truth on the spawn packet
instead of a number that was already stale when it was sent:

.. tabs::
 .. code-tab:: gdscript GDScript

    var countdown_target := 0

    func _init() -> void:
        Netw.configure_property(self, &"countdown_target").on_spawn()

    var seconds_left: float:
        get:
            if countdown_target == 0:
                return 0.0
            var api := Netw.of(self)
            return maxf(0.0, float(countdown_target - api.tick) / api.tickrate)

    # Server only. Clients read seconds_left and display it.
    func start_countdown(seconds: int) -> void:
        var api := Netw.of(self)
        countdown_target = api.tick + seconds * api.tickrate

Only the server needs the "countdown finished" edge, and the server does
not need a network mechanism to learn its own timer expired — it compares
:ref:`tick <class_NetwMultiplayer>` against the target in its own process
loop. Clients need a number to display, which is exactly what the property
gives them.

Readiness is the same shape with a dictionary. The client asks, the server
decides and writes, and every peer reads the result:

.. tabs::
 .. code-tab:: gdscript GDScript

    var ready_peers: Dictionary[int, bool] = { }

    func _init() -> void:
        Netw.configure_property(self, &"ready_peers").on_spawn()
        Netw.configure_rpc(request_ready)

    # Player request.
    func request_ready(is_ready: bool) -> void:
        var peer_id := multiplayer.get_remote_sender_id()
        ready_peers[peer_id] = is_ready
        ready_peers = ready_peers  # re-assign so the property replicates

    func everyone_ready() -> bool:
        return not ready_peers.is_empty() and not ready_peers.values().has(false)

The subtlety worth knowing: when a not-ready player leaves, the remaining
players may now all be ready. Erase the departing peer's row in your
:ref:`on_participant_left() <class_NetwSceneHandle_method_on_participant_left>`
callback and re-check, or
the match never starts. Whether that should start the match is game policy,
which is why it lives in your code rather than in the addon.

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
copy of the template into the target scene through
:ref:`NetwSceneHandle.add_player() <class_NetwSceneHandle_method_add_player>`.
For everything else (NPCs, projectiles, loot) there are two helpers:

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
