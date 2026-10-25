<p align="center">
  <img src="assets/networked.svg" alt="networked logo" width="300">
</p>

# networked


[![documentation](https://img.shields.io/badge/documentation-class%20reference-green?logo=readthedocs&logoColor=white&labelColor=CFC9C8&color=6BCD69)](https://networked.readthedocs.io/en/latest/classes/index.html)
[![build](https://img.shields.io/github/actions/workflow/status/diejor/networked/ci.yml?label=build&logo=github&logoColor=white&labelColor=CFC9C8&color=DBDCB8)](https://github.com/diejor/networked/actions/workflows/ci.yml)
[![play](https://img.shields.io/badge/play-examples-fa5c5c?logo=cloudflare&logoColor=white)](https://netw-examples.pages.dev)
[![chat](https://img.shields.io/badge/chat-discord-646FA9?logo=discord&logoColor=white&labelColor=CFC9C8&color=646FA9)](https://discord.gg/7bXbVy9Zfu)

A multiplayer framework for Godot 4, built as a GDExtension.

It replaces Godot's
[`SceneMultiplayer`](https://docs.godotengine.org/en/stable/classes/class_scenemultiplayer.html)
with `NetwMultiplayer`, which keeps its whole API. Existing `@rpc`,
`MultiplayerSynchronizer` and `MultiplayerSpawner` code works unchanged once
the addon is installed.

## Install

Download `networked` from the
[latest release](https://github.com/diejor/networked/releases/latest) and
unzip it into your project's root. `networked_steam`, `networked_discord` and
`networked_nakama` are optional and unzip beside it.

## Demos

Play them in the browser at [netw-examples.pages.dev](https://netw-examples.pages.dev).

- [`bomber`](examples/bomber), Godot's multiplayer bomber demo
- [`racing`](examples/racing), Kenney's Starter Kit Racing
- [`rocket_league`](examples/rocket_league), cars and a ball
- [`fps`](examples/fps), Kenney's Starter Kit FPS
- [`playground`](examples/playground), players pushing 900 cubes around
- [`multiple_levels`](examples/multiple_levels), players moving between levels
- [`whiteboard`](examples/whiteboard), a shared drawing board

To run them from a clone of this repository,
[build the GDExtension](extension/BUILDING.md) before opening the project in
Godot.

## Replicate a property

Configure a property once and write it normally. Every change reaches the
other peers.

```gdscript
func _init() -> void:
    Netw.configure_property(self, &"position").state()      # the server writes it
    Netw.configure_property(self, &"steering").input()      # the controller writes it
    Netw.configure_property(self, &"aim_angle").broadcast() # the controller sends it
    Netw.configure_property(self, &"fuel")                  # sent when you ask
```

A property marked `.state()`, `.input()` or `.broadcast()` is sent every tick
it changes. Any other is sent with `sync_property`.

```gdscript
func refuel() -> void:
    fuel = 100.0
    Netw.sync_property(self, &"fuel")
```

Smooth a property between updates.

```gdscript
Netw.configure_property(self, &"position").interpolate(
        NetwInterpolate.new().lerp().smooth(0.05).to(&"position"))
```

Send it in fewer bits.

```gdscript
Netw.configure_property(self, &"turret_yaw").broadcast().quantize(
        NetwQuantizeAngle.new().bits(12).centered())
```

Both work on a property a `MultiplayerSynchronizer` already replicates.

## Pass nodes through RPCs

Configure the method, then call it. A node passed as an argument arrives as
the receiver's own copy of that node.

```gdscript
func _init() -> void:
    Netw.configure_rpc(self.apply_stun)

@rpc("authority", "reliable")
func apply_stun(attacker: Node) -> void:
    play_stun(attacker)

Netw.rpc(target.apply_stun, attacker_root)
```

Renaming or moving a node never breaks a call. A call that names a node the
receiver has not spawned yet waits for the spawn.

## Replicate a signal

Configure the signal, then emit it on every peer with one call.

```gdscript
func _init() -> void:
    Netw.configure_signal(self.exploded)

Netw.emit_entity_signal(self.exploded, position)
```

## Spawn

Configure a spawn function, then call it. Every peer builds the node, and the
call returns this peer's copy for you to add.

```gdscript
func _init() -> void:
    Netw.configure_spawn(make_bullet)

func make_bullet(direction: Vector2) -> Node:
    var bullet := BulletScene.instantiate()
    bullet.direction = direction
    return bullet

muzzle.add_child(Netw.spawn(make_bullet, direction))
```

## Reparent

`Node.reparent` is replicated.

```gdscript
player.reparent(arena.get_node(^"Spawns"))
```

## More

- [Prediction and rollback](https://networked.readthedocs.io/en/latest/classes/class_netwprediction.html#class-netwprediction).
  A controller runs its entity ahead and corrects when the server disagrees.
- [Interest management](https://networked.readthedocs.io/en/latest/classes/class_netwinterest.html#class-netwinterest).
  Each peer receives only the entities it can see.
- [Persistence](https://networked.readthedocs.io/en/latest/classes/class_netwpersistence.html#class-netwpersistence).
  Properties save and load with their entity, through a
  [database](https://networked.readthedocs.io/en/latest/classes/class_netwdatabase.html#class-netwdatabase)
  you choose.
- [Steam](https://networked.readthedocs.io/en/latest/classes/class_steamsocial.html#class-steamsocial).
  Friends, invites, lobbies and parties, with Steam sign in.
- [Discord](addons/networked_discord/README.md).
  Presence, friends, parties with voice and Discord sign in.

## Credits

- The lag compensation, prediction, and rollback architecture is based
  on [netfox](https://github.com/foxssake/netfox). Its documentation,
  documented issues, and Discord discussions were valuable references.
- Simulated network conditions on a real connection follow
  [laggy-multiplayer-peer](https://github.com/ricmzn/laggy-multiplayer-peer)
  by Ricardo Nunes.
- The WebRTC signaler, transport, and related work are based on
  [Tube](https://github.com/koopmyers/tube/).
- The wire architecture is based on
  [yojimbo](https://github.com/mas-bandwidth/yojimbo).
- Thanks to
  [godot_debug_draw_3d](https://github.com/DmitriySalnikov/godot_debug_draw_3d)
  for the approach the C# bindings use. The addon exports C function pointers
  and the generated C# calls through them.
- *Multiplayer Game Programming. Architecting Networked Games* by Joshua
  Glazer and Sanjay Madhav was insightful reading for general netcode
  architecture.
