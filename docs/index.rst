:github_url: hide

Networked
=========

Networked is a multiplayer framework for Godot 4, shipped as a GDExtension.
Its session, :ref:`NetwMultiplayer <class_NetwMultiplayer>`, is a strict
superset of :godot:`SceneMultiplayer <SceneMultiplayer>`, and the extension
installs it as the project's default multiplayer interface. Every
:godot:`SceneTree <SceneTree>` gets it with no node authored and no scene
changed, so existing ``@rpc`` code keeps working and the declarations below
are what you add.

Set ``networked/install_as_default`` to ``false`` in the project settings to
opt out and install the session yourself.

Declaring replication
---------------------

Declare a field's delivery once, then write the variable normally. The
session's sync pump ships every change.

.. code-block:: gdscript

    func _init() -> void:
        Netw.configure_property(self, &"position").state()
        Netw.configure_rpc(self.apply_stun)

    @rpc("authority", "reliable")
    func apply_stun(attacker: Node) -> void:
        play_stun(attacker)

:ref:`Netw <class_Netw>` is the front door. It resolves the session for any
node, carries the ``configure_*`` declarations, and addresses RPCs by an id
rather than by node path, so renaming or moving a node never breaks one.

Beside the engine's nodes
-------------------------

Keep the nodes you authored. These declarations sit beside them.

Beside a MultiplayerSynchronizer
................................

Smooth a field the :godot:`MultiplayerSynchronizer` already ships. A
declaration that only interpolates writes nothing, so the synchronizer still
owns the value.

.. code-block:: gdscript

    Netw.configure_property(self, &"position").interpolate(
            NetwInterpolate.new().lerp().smooth(0.05).to(&"position"))

Its list names properties by ``NodePath`` and records no types, so every value
is sent with its type spelled out beside it. Declare the field instead and a
:ref:`NetwQuantize <class_NetwQuantize>` sends it in the bits you ask for.

.. code-block:: gdscript

    Netw.configure_property(self, &"turret_yaw").broadcast().quantize(
            NetwQuantizeAngle.new().bits(12).centered())

Beside a MultiplayerSpawner
...........................

Declare the spawned root a scene. It becomes a world players are let into one
at a time.

.. code-block:: gdscript

    func _init() -> void:
        Netw.configure_multiplayer_scene(self).labeled(&"Arena")

Everything under that root belongs to the world, nested spawns included. No
per-synchronizer visibility filter, and no tree order to get right.

Swap worlds with the call you already know, made multiplayer-correct.

.. code-block:: gdscript

    Netw.change_scene_to_file(self, "res://arena.tscn")

Move between them with plain :godot:`Node.reparent <Node#class-node-method-reparent>`.

.. code-block:: gdscript

    player.reparent(arena.get_node(^"Spawns"))

The move is detected where the node lands, not declared at the call, so
``remove_child`` then ``add_child`` works the same. Children come along, and
every peer that can see the new scene is caught up on what moved into it.

Beside an @rpc
..............

The annotation stays. Register the method and you can pass a :godot:`Node` as
an argument, which arrives as the receiver's own copy of it.

.. code-block:: gdscript

    Netw.rpc(target.apply_stun, attacker_root)

A call naming a node that peer has not spawned yet waits for the spawn, so the
call and the spawn may cross in either order.

Documentation status
--------------------

**There is currently no hand-written guide.** The previous manual documented
an earlier architecture built around an authored ``MultiplayerTree`` node, and
it was deleted rather than repaired, because a guide that argues for a
superseded design is worse than no guide while it still reads as maintained.
A replacement is written once the published surface settles.

Until then the :ref:`class reference <toc-class-ref>` is the documentation,
generated from the same XML the in-editor help serves. Start at
:ref:`Netw <class_Netw>` and :ref:`NetwMultiplayer <class_NetwMultiplayer>`.

.. toctree::
   :maxdepth: 1
   :caption: Contributing

   contributing/index

.. toctree::
   :maxdepth: 2
   :caption: Class reference
   :name: toc-class-ref

   classes/index
