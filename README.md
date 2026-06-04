# networked

[![documentation](https://img.shields.io/badge/documentation-online-green?logo=readthedocs&logoColor=white&labelColor=CFC9C8&color=6BCD69)](https://networked.readthedocs.io/en/latest/?badge=latest)
[![build](https://img.shields.io/github/actions/workflow/status/diejor/networked/ci.yml?label=build&logo=github&logoColor=white&labelColor=CFC9C8&color=DBDCB8)](https://github.com/diejor/networked/actions/workflows/ci.yml)
[![play](https://img.shields.io/badge/play-Bomber%20demo-fa5c5c?logo=itch.io&logoColor=white)](https://diejor.itch.io/bomber)
[![chat](https://img.shields.io/badge/chat-discord-646FA9?logo=discord&logoColor=white&labelColor=CFC9C8&color=646FA9)](https://discord.gg/7bXbVy9Zfu)

**A proof-of-concept multiplayer framework for Godot 4.**

I built this because Godot's built-in multiplayer APIs, while powerful, felt 
overwhelming to wire together correctly. `networked` is my attempt to wrap 
`SceneMultiplayer` into something friendlier, especially for people who just 
want players to spawn, move smoothly, and teleport between levels without 
dropping sync.

This is not a battle-tested AAA solution. It is one developer's experiment, 
shared in the hope that it helps someone else get started with multiplayer.

## Try It in Your Browser

The [`examples/bomber`](examples/bomber) game runs live at 
**[diejor.itch.io/bomber](https://diejor.itch.io/bomber)**. It is a modified
version of Godot's [`multiplayer_bomber`][godot-bomber] demo, rebuilt with the
`networked` addon and a WebRTC WebTorrent lobby system. You can host and play
with other people completely P2P, with no game server to run.

[godot-bomber]: https://github.com/godotengine/godot-demo-projects/tree/3.5-9e68af3/networking/multiplayer_bomber

## What It Does

`networked` provides an opinionated layer on top of Godot's 
[High-level multiplayer](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html)

- **Scene-aware spawning:** `MultiplayerSceneManager` + `SpawnerComponent` 
handle player spawn and despawn.

- **Smooth interpolation:** `TickInterpolator` smooths network snapshots with 
smart dilation to reduce jitter.

- **Teleport transitions:** `TPComponent` lets players move between levels 
without breaking replication.

- **State persistence:** `NetwDatabase` + `SaveComponent` save and load player 
data.

- **Flexible transport:** `BackendPeer` resources for ENet, WebSocket, WebRTC, 
and local loopback. Optional duck-typed support for the `tube` addon if you need 
WebRTC matchmaking.

## Requirements

- Godot 4.2+
- GDScript (C# support is not available yet)
