# networked

**A proof-of-concept multiplayer framework for Godot 4.**

I built this because Godot's built-in multiplayer APIs, while powerful, felt overwhelming to wire together correctly. `networked` is my attempt to wrap `SceneMultiplayer` into something friendlier -- especially for people who just want players to spawn, move smoothly, and teleport between levels without dropping sync.

This is not a battle-tested AAA solution. It is one developer's experiment, shared early in the hope that it helps someone else get started with multiplayer.

## What It Does

`networked` provides a thin, opinionated layer on top of Godot's native multiplayer system. It does not replace `SceneMultiplayer` or `MultiplayerSpawner`; it organizes them.

- **Scene-aware spawning:** `MultiplayerSceneManager` + `SpawnerComponent` handle player spawn and despawn across multiple scenes.
- **Smooth interpolation:** `TickInterpolator` smooths network snapshots with smart dilation to reduce jitter.
- **Teleport transitions:** `TPComponent` lets players move between levels without breaking replication.
- **State persistence:** `NetwDatabase` + `SaveComponent` save and load player data.
- **Flexible transport:** `BackendPeer` resources for ENet, WebSocket, WebRTC, and local loopback. Optional duck-typed support for the `tube` addon if you need WebRTC matchmaking.

## Requirements

- Godot 4.2+
- GDScript (C# support is not available yet)

## Examples

See the `examples/daily/` folder for a small top-down demo with a lobby, player spawning, and scene transitions.

## Running Tests

Tests use [GdUnit4](https://github.com/MikeSchulze/gdUnit4). Run them from the editor or headlessly:

```bash
godot --path . -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless --ignoreHeadlessMode --ignore-error-breaks -a res://tests/
```

## What's Next

This framework is evolving. Here is what I am thinking about next:

- **Better documentation and tutorials:** The code is documented, but step-by-step guides are sparse.
- **More examples:** A physics-based example and a Web export demo.
- **C# support:** If there is demand, I would like to make the API accessible to C# developers. This likely means a GDExtension rewrite, which is a significant effort.
- **Stability and API cleanup:** Some APIs are still settling. Breaking changes are possible until a 1.0 release.

If this helps you, or if it breaks, please open an issue. I am building this in public.
