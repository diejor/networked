:github_url: hide

Networked
=========

Networked is a multiplayer framework for Godot 4, built as a GDExtension. It
replaces :godot:`SceneMultiplayer <SceneMultiplayer>` with
:ref:`NetwMultiplayer <class_NetwMultiplayer>`, which keeps its whole API, so
existing ``@rpc`` code works unchanged once the addon is installed.

Set ``networked/install_as_default`` to ``false`` in the project settings to
install the multiplayer yourself.

Start with :ref:`Netw <class_Netw>`. It finds the session of any node and
holds the ``configure_*`` methods.

.. code-block:: gdscript

    func _init() -> void:
        Netw.configure_property(self, &"position").state()
        Netw.configure_rpc(self.apply_stun)

    @rpc("authority", "reliable")
    func apply_stun(attacker: Node) -> void:
        play_stun(attacker)

Beside the engine's nodes
-------------------------

Your existing multiplayer nodes keep working, and these calls add to them.

Beside a MultiplayerSynchronizer
................................

Smooth a property the :godot:`MultiplayerSynchronizer` already replicates. A
property that is only interpolated is still written by the synchronizer.

.. code-block:: gdscript

    Netw.configure_property(self, &"position").interpolate(
            NetwInterpolate.new().lerp().smooth(0.05).to(&"position"))

A synchronizer sends each value with its type beside it. A
:ref:`NetwQuantize <class_NetwQuantize>` sends a configured property in the
bits you ask for.

.. code-block:: gdscript

    Netw.configure_property(self, &"turret_yaw").broadcast().quantize(
            NetwQuantizeAngle.new().bits(12).centered())

Beside a MultiplayerSpawner
...........................

Make a spawned root a :ref:`NetwScene <class_NetwScene>`. Players see what is
under it only while they are in it, nested spawns included.

.. code-block:: gdscript

    func _init() -> void:
        Netw.configure_scene(self).labeled(&"Arena")

Move a node between scenes with
:godot:`Node.reparent <Node#class-node-method-reparent>`. Its children move
with it, and every peer in the new scene receives it.

.. code-block:: gdscript

    player.reparent(arena.get_node(^"Spawns"))

Beside an @rpc
..............

Configure the method and a :godot:`Node` can be passed as an argument. It
arrives as the receiver's own copy.

.. code-block:: gdscript

    Netw.rpc(target.apply_stun, attacker_root)

Renaming or moving a node never breaks a call. A call that names a node the
receiver has not spawned yet waits for the spawn.

Writing in C#
-------------

The addon includes generated C# bindings under
``addons/networked/native_api/cs/``. The project's ``.csproj`` needs no
changes.

.. toctree::
   :maxdepth: 2
   :caption: Class reference
   :name: toc-class-ref

   classes/index
