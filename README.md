<p align="center">
  <img src="assets/networked.svg" alt="networked logo" width="300">
</p>

# networked


[![documentation](https://img.shields.io/badge/documentation-class%20reference-green?logo=readthedocs&logoColor=white&labelColor=CFC9C8&color=6BCD69)](https://networked.readthedocs.io/en/latest/classes/index.html)
[![build](https://img.shields.io/github/actions/workflow/status/diejor/networked/ci.yml?label=build&logo=github&logoColor=white&labelColor=CFC9C8&color=DBDCB8)](https://github.com/diejor/networked/actions/workflows/ci.yml)
[![play](https://img.shields.io/badge/play-examples-fa5c5c?logo=cloudflare&logoColor=white)](https://netw-examples.pages.dev)
[![chat](https://img.shields.io/badge/chat-discord-646FA9?logo=discord&logoColor=white&labelColor=CFC9C8&color=646FA9)](https://discord.gg/7bXbVy9Zfu)

A drop-in replacement for Godot's [`SceneMultiplayer`](https://docs.godotengine.org/en/stable/classes/class_scenemultiplayer.html), shipped as a GDExtension.

`NetwMultiplayer` is a strict superset of `SceneMultiplayer`'s surface, and the
extension installs it as the project's default multiplayer interface at load.
Every `SceneTree` gets it with **no node authored and no scene changed**, your
existing `@rpc`, `MultiplayerSynchronizer`, `MultiplayerSpawner` code keeps working.

> [!TIP]
> Set `networked/install_as_default` to `false` in the project settings to opt 
> out and install the session yourself.

## Demos

[`examples/bomber`](examples/bomber) · [`examples/racing`](examples/racing) · [`examples/rocket_league`](examples/rocket_league) · [`examples/quick_start`](examples/quick_start)

## Replicate a property

Declare a field once. Write it normally. The session ships every change.

```gdscript
func _init() -> void:
    Netw.configure_property(self, &"position").state()      # server owns it
    Netw.configure_property(self, &"steering").input()      # controller owns it
    Netw.configure_property(self, &"aim_angle").broadcast() # server trusts
    Netw.configure_property(self, &"fuel")                  # on demand
```

A field marked `.state()`, `.input()` or `.broadcast()` joins a per-tick set and
needs no send call. An unmarked field ships when you push it:

```gdscript
func refuel() -> void:
    fuel = 100.0
    Netw.sync_property(self, &"fuel")
```

You can smooth out properties.

```gdscript
Netw.configure_property(self, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.05).to(&"position"))
```

And also quantize.

```gdscript
Netw.configure_property(self, &"turret_yaw").broadcast().quantize(
		NetwQuantizeAngle.new().bits(12).centered())
```

> [!NOTE]
> You can configure your `MultiplayerSynchronizers` properties the same way!

## Pass nodes through RPCs

Register the method once, then call it. Pass a node as an argument and the
other side receives its own live copy of that node.

```gdscript
func _init() -> void:
    Netw.configure_rpc(self.apply_stun)

@rpc("authority", "reliable")
func apply_stun(attacker: Node) -> void:
    play_stun(attacker)

Netw.rpc(target.apply_stun, attacker_root)
```

Calls are addressed by an id rather than by node path, so renaming or moving a
node never breaks one. A call naming a node that peer has not spawned yet waits 
for the spawn, so the call and the spawn may cross in either order.

## Replicate a signal

Allowlist the signal, then emit it everywhere with one call.

```gdscript
func _init() -> void:
	Netw.configure_signal(self.exploded)

Netw.emit_entity_signal(exploded)   # local listeners and every peer's copy
```

> [!TIP]
> You can rename, move the entities and race spawn packets with 
> replicated signals the same way as to `@rpc`.

## Spawn

Register a spawn function, then call it. The node is constructed on every peer
and returned locally for you to place.

```gdscript
func _init() -> void:
	Netw.configure_spawn(_spawn_bullet)

func _spawn_bullet(dir: Vector2, tier: int) -> Node:
	var b := BulletScene.instantiate()
	b.setup(dir, tier)
	return b

muzzle.add_child(Netw.spawn(_spawn_bullet, dir, 2))
```

## Change scene

Like `SceneTree.change_scene_to_file`, made multiplayer-correct. On the server it
applies. On a client it becomes a request the server decides.

```gdscript
var promise := Netw.change_scene_to_file(self, "res://match.tscn")
if await promise.completed != OK:
	status.text = "Could not start the match."
```

## Reparent

Yes, you can move nodes around with `Node.reparent`.

```gdscript
player.reparent(arena.get_node(^"Spawns"))
```

Reparenting is a replicated action, no longer triggering a `DESPAWN`/`SPAWN` 
dance when moving nodes around.

## More

- **Prediction and rollback.** Declaring prediction on a field arms rewind.
  There is no engine to turn on separately.
- **Interest management.** `Netw.configure_interest` puts a node on a layer, and
  a send reaches only the peers that layer lets in.
- **Persistence.** `Netw.configure_persistence` picks the fields to save, then
  reads and writes them straight off the live scene.
- **Transports.** ENet, WebSocket, WebRTC and a local loopback. Listen-server,
  dedicated server, local play and host-relay P2P are all the same session.
