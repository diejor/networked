# networked

[![Documentation Status](https://readthedocs.org/projects/networked/badge/?version=latest)](https://networked.readthedocs.io/en/latest/?badge=latest)
[![CI](https://github.com/diejor/networked/actions/workflows/ci.yml/badge.svg)](https://github.com/diejor/networked/actions/workflows/ci.yml)

**A proof-of-concept multiplayer framework for Godot 4.**

I built this because Godot's built-in multiplayer APIs, while powerful, felt 
overwhelming to wire together correctly. `networked` is my attempt to wrap 
`SceneMultiplayer` into something friendlier, especially for people who just 
want players to spawn, move smoothly, and teleport between levels without 
dropping sync.

This is not a battle-tested AAA solution. It is one developer's experiment, 
shared in the hope that it helps someone else get started with multiplayer.

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
