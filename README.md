# networked

[![documentation](https://img.shields.io/badge/documentation-online-green?logo=readthedocs&logoColor=white&labelColor=CFC9C8&color=6BCD69)](https://networked.readthedocs.io/en/latest/?badge=latest)
[![build](https://img.shields.io/github/actions/workflow/status/diejor/networked/ci.yml?label=build&logo=github&logoColor=white&labelColor=CFC9C8&color=DBDCB8)](https://github.com/diejor/networked/actions/workflows/ci.yml)
[![play](https://img.shields.io/badge/play-Bomber%20demo-fa5c5c?logo=itch.io&logoColor=white)](https://diejor.itch.io/bomber)
[![chat](https://img.shields.io/badge/chat-discord-646FA9?logo=discord&logoColor=white&labelColor=CFC9C8&color=646FA9)](https://discord.gg/7bXbVy9Zfu)

## Quick Reference

- **Bomber Demo:** [`examples/bomber`](examples/bomber)
- **Documentation:** [Quick Start Guide](https://networked.readthedocs.io/en/latest/getting_started/quick_start.html)

## Addon Overview

`networked` wraps Godot's [High-level multiplayer](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html) into a single Node [MultiplayerTree](https://networked.readthedocs.io/en/latest/manual/multiplayer_tree.html) that owns the entire lifecycle, from connect to disconnect.

### Notable Features

- **`TickInterpolator`:** Smooths network snapshots with smart dilation to minimize jitter.
- **`MultiplayerSceneManager`:** Built on top of an in-house [Interest Management System](https://networked.readthedocs.io/en/latest/manual/interest_management.html), allowing you to host, spawn, and swap multiple scenes concurrently within the same `SceneTree`.
- **`TPComponent`:** Connects scenes and handles teleport transitions between levels without dropping synchronization.
- **`SaveComponent`:** Real-time state persistence and writes for player data.

## Supported Architectures & Integrations

- **Flexible Architectures:** Supports listen-server and dedicated server, easily swap between Client-Server and P2P all driven by [MultiplayerTree](https://networked.readthedocs.io/en/latest/manual/multiplayer_tree.html).
- **Transport Backends & Steam:** Modular support for WebSocket, WebRTC, ENet, and native Steam matchmaking (see [Transport Backends Guide](https://networked.readthedocs.io/en/latest/manual/transport_backends.html)).
- **Robust Integration Testing:** Full end-to-end integration tests are supported using the custom in-process [LocalMultiplayerPeer](file:///c:/Users/diejor/projects/networked/addons/networked/transport/local/local_multiplayer_peer.gd), test your games with [Testing API](https://networked.readthedocs.io/en/latest/manual/testing.html).