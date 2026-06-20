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

- **`LagCompensation`:** CS-style client prediction and server reconcilation.
- **`MultiplayerInterpolator`:** Smooth network snapshots with time dilation to minimize jitter.
- **`MultiplayerSceneManager`:** Built on top of a robust and extensible [Interest Management System](https://networked.readthedocs.io/en/latest/manual/interest_management.html), allowing you to host, spawn, and swap multiple scenes concurrently within the same `SceneTree`.
- **`TPComponent`:** Move players between scenes and handle teleport transitions between levels.
- **`ConnectBrowser`:** Connect multiple lobby providers and backends in a single UI and [Pre-Game Connection Model](https://networked.readthedocs.io/en/latest/manual/pre_game_connection.html).
- **`SaveComponent`:** Real-time state persistence and writes for player data.

## Supported Architectures & Integrations

- **Flexible Architectures:** Supports listen-server, dedicated server, local play and P2P (through host-relay) all driven by [MultiplayerTree](https://networked.readthedocs.io/en/latest/manual/multiplayer_tree.html).
- **Transport Backends & Steam:** Modular support for WebSocket, WebRTC, ENet, and Steam matchmaking (see [Transport Backends Guide](https://networked.readthedocs.io/en/latest/manual/transport_backends.html)). Host everywhere design a single UI.
- **Robust Integration Testing:** Full end-to-end integration tests are supported using the in-process peer [LocalMultiplayerPeer](file:///c:/Users/diejor/projects/networked/addons/networked/transport/local/local_multiplayer_peer.gd), test your games with [Testing API](https://networked.readthedocs.io/en/latest/manual/testing.html).

## Credits

- **netfox:** The lag compensation architecture was inspired by the [netfox](https://github.com/foxssake/netfox) Godot addon. The original creator is [Tamás Gálffy](https://github.com/foxssake).
- **Tube:** The WebRTC tracker signaling architecture and user-friendly room code concepts were inspired by and adapted from the [Tube](https://github.com/koopmyers/tube) Godot addon created by [koopmyers](https://github.com/koopmyers).
