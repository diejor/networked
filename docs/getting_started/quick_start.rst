.. _doc_quick_start:

Quick start
===========

This page walks you through the smallest project that runs a networked session
with Networked: a single scene, a single player, and a single transport. By the
end you will have a window where you can host a server, connect to it from a
second instance of the editor, and watch a character move on both screens at
the same time.

Networked is built directly on top of Godot's high-level multiplayer API. If
you have never read the engine's :godot:`SceneMultiplayer <SceneMultiplayer>`
chapter before, you do not need to, this page introduces every concept that
you need, but the engine's :godot:`MultiplayerSpawner <MultiplayerSpawner>`
and :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` documentation
makes a good companion read once you are done.

.. note::

    The code on this page uses GDScript. C# is not currently supported by the
    addon.

Before you start
----------------

You should already have:

- A Godot project with the ``addons/networked/`` folder installed and the
  *Networked* plugin enabled in :menu:`Project > Project Settings > Plugins`.
- Familiarity with the editor's scene dock and inspector. If you have built a
  single-player Godot scene before, you have enough.

If you only want to read along, the snippets in this page are taken from the
``examples/daily`` project that ships with the source repository. Open it in
the editor and use :kbd:`F5` to follow along with a working scene.

The mental model
----------------

Networked turns a Godot scene tree into a *session*. A session is anything
that can be hosted or joined: a listen-server with two players, a dedicated
server, a hot-seat lobby on a single machine. The three nodes you
will meet in this quick start are the moving parts of every session.

- :ref:`MultiplayerTree <class_MultiplayerTree>` is the entry point. You add
  it to your scene, give it a transport (a
  :ref:`BackendPeer <class_BackendPeer>`), and call its session entry methods:
  :ref:`join or host <class_MultiplayerTree_method_join_or_host>`,
  :ref:`join <class_MultiplayerTree_method_join>`, or
  :ref:`host player <class_MultiplayerTree_method_host_player>`. It owns its own
  :godot:`SceneMultiplayer <SceneMultiplayer>` and installs it onto the scene
  tree, so every descendant gets the correct :godot:`multiplayer <Node#class_node_property_multiplayer>` property
  automatically.
- :ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` (optional for
  this first project) replicates whole levels to clients. For a single-scene
  game you can skip it: dropping a world scene directly under the tree makes
  Networked auto-configure a one-scene manager behind the scenes.
- :ref:`SpawnerComponent <class_SpawnerComponent>` marks one node in a scene
  as the spawnable player template. It extends Godot's
  :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` and bundles
  initial state (position, peer authority, custom properties) into the spawn
  packet so clients see the entity in the right state on the first frame.

In the rest of this page you will wire all three nodes together, then add
input to a player so they can walk around.

Setting up the session
----------------------

Create a new empty scene with a :godot:`Node2D <Node2D>` root and save it as
``main.tscn``. Add a child :godot:`Node <Node>` named ``Client`` (this name
is arbitrary. Attach the :ref:`MultiplayerTree <class_MultiplayerTree>` 
script to the new node.

In the inspector for ``Client``, click :button:`<empty>` next to the
:button:`Backend` property and choose :menu:`New WebSocketBackend`. The
WebSocket backend is convenient for early testing because it works in HTML5
exports without any extra configuration. If you target desktop only, pick
:menu:`New ENetBackend` instead. Both implement the same
:ref:`BackendPeer <class_BackendPeer>` interface, so the rest of this page
applies unchanged.

You now have a tree that can host or join, but no world for players to spawn
into. Let's build one.

Building a world scene
----------------------

Create a second scene, ``player.tscn``, with a
:godot:`CharacterBody2D <CharacterBody2D>` root, a child
:godot:`Sprite2D <Sprite2D>` for visuals, and a
:godot:`CollisionShape2D <CollisionShape2D>`. Save it.

Now add a :ref:`SpawnerComponent <class_SpawnerComponent>` child to the
:godot:`CharacterBody2D <CharacterBody2D>`. The component automatically renames itself to
:ref:`SpawnerComponent <class_SpawnerComponent>` and registers a unique name. In the *Replication* panel
at the bottom of the editor, add a single property: the body's :godot:`position <Node2D#class_node2d_property_position>`,
with the *Spawn* checkbox enabled. This tells the server to bundle the
player's starting position into the spawn packet so the entity appears in the
right place on every client's first frame.

.. note::

    All flags other than *Spawn* are coerced to off at runtime,
    :ref:`SpawnerComponent <class_SpawnerComponent>` only uses the
    replication config for the spawn snapshot. For continuous state
    replication, add a sibling
    :godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` and configure
    it independently.

Set the body's *Authority Mode* on the component to :ref:`CLIENT <class_SpawnerComponent_constant_CLIENT>` if you want
the connecting player to drive their own movement. This is the common case
for player avatars: server stays the source of truth for spawn and despawn,
but the client peer owns the body itself and can read input from
:godot:`Input <Input>`.

Finally, create the level scene ``level.tscn`` with a :godot:`Node2D <Node2D>`
root and instance ``player.tscn`` as a child. The player you place here is a
*template*: it sits in the scene at edit time, but the runtime spawn flow
copies it for each connecting peer. Add a :godot:`MultiplayerSpawner <MultiplayerSpawner>` 
that tracks ``player.tscn`` in the auto-spawn list.

Back in ``main.tscn``, drag ``level.tscn`` as a child of the ``Client`` node.
Because the level contains a :ref:`SpawnerComponent <class_SpawnerComponent>`
descendant, the tree's :godot:`_enter_tree() <Node#class_node_private_method__enter_tree>` will detect it on play and silently
substitute it for a one-scene
:ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` configured to
spawn this level on startup.

Joining the session
-------------------

You now have a scene that can host, but nothing tells it to. The simplest
way to drive the connection is from a script attached to the ``Main`` root.
A :ref:`JoinPayload <class_JoinPayload>` describes who is connecting and
where they want to spawn. Transport identity (backend, address) is passed
separately to the entry method:

.. tabs::
 .. code-tab:: gdscript GDScript

    extends Node2D

    @onready var client: MultiplayerTree = $Client
    const LEVEL = preload("res://level.tscn")

    func _ready() -> void:
        var spawner_path := SceneNodePath.new()
        spawner_path.scene_path = LEVEL.resource_path
        spawner_path.node_path = "Player/%SpawnerComponent"

        var join := JoinPayload.new()
        join.username = "alice"
        join.spawn = SpawnerComponentPolicy.from_scene_node_path(spawner_path).to_dict()

        var target := JoinTarget.new()
        target.backend = client.backend
        target.address = "localhost"

        await client.join_or_host(target, join)

:ref:`join_or_host() <class_MultiplayerTree_method_join_or_host>`
queries the address for a live local server first; if one answers, it joins
as a client, otherwise it falls back to hosting. For LAN or internet
servers, build a :ref:`JoinTarget <class_JoinTarget>` for the server and
call :ref:`join() <class_MultiplayerTree_method_join>` instead.

.. tip::

    Dropping a world scene (one containing a ``SpawnerComponent``) directly
    as a child of the :ref:`MultiplayerTree <class_MultiplayerTree>` auto-creates
    a :ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>` and assigns
    the tree a :ref:`SpawnerComponentPolicy <class_SpawnerComponentPolicy>`, so
    joining players spawn automatically without any spawn-handling code. A tree
    without a dropped world scene leaves ``spawn_policy`` unset, so you control
    spawning from
    :ref:`player_joined <class_MultiplayerTree_signal_player_joined>` instead.

Press :kbd:`F5` to launch the project. Then, from the editor, choose
:menu:`Debug > Run Multiple Instances` and set it to ``2``. Run the project
again: two windows appear, each spawns a player with the username they were
given, and you should see both characters on each screen.

Adding player input
-------------------

Right now the players spawn but do not move. Add the following script to
``player.tscn``'s root so each peer drives only the body they own:

.. tabs::
 .. code-tab:: gdscript GDScript

    extends CharacterBody2D

    @export var speed: float = 200.0

    func _physics_process(_delta: float) -> void:
        if not is_multiplayer_authority():
            return
        var dir := Input.get_vector(
            "ui_left", "ui_right", "ui_up", "ui_down"
        )
        velocity = dir * speed
        move_and_slide()

The :godot:`is_multiplayer_authority <Node#class_node_method_is_multiplayer_authority>`
guard is essential: every peer runs :godot:`_physics_process() <Node#class_node_private_method__physics_process>` on every
:godot:`CharacterBody2D <CharacterBody2D>` in the level, but only the peer
that owns this particular body should be the one writing to ``velocity``.

The :ref:`CLIENT <class_SpawnerComponent_constant_CLIENT>` authority mode you picked earlier means
:ref:`SpawnerComponent <class_SpawnerComponent>` sets that peer's id as the
body's multiplayer authority right after spawn, so the right player is in
control with no extra wiring.

To replicate that movement back to the other peer, add a sibling
:godot:`MultiplayerSynchronizer <MultiplayerSynchronizer>` to the player
scene and register the body's :godot:`position <Node2D#class_node2d_property_position>` with replication mode *On Change*
and the *Sync* flag enabled. Run the project again. Both players can now
walk around, and each peer sees the other in real time.

Where to go next
----------------

You now have the building blocks every Networked session uses: a tree, a
backend, a level, and a spawnable player. From here, the
:ref:`manual <doc_manual_overview>` walks through each subsystem in depth, including
scene transitions with :ref:`TPComponent <class_TPComponent>`, saved data
with :ref:`SaveComponent <class_SaveComponent>`, and the
:ref:`NetwContext <class_NetwContext>` facade used by most user scripts.

If you want to see a complete, larger project, the ``examples/bomber`` scene
in the repository runs the same APIs across a lobby, multiple connected
players, and a per-peer authoritative bomb spawner.
