# Bomber Example

This example is a modified version of Godot's
[`multiplayer_bomber`][godot-bomber] demo. It keeps the familiar bomber game
loop, but replaces the original networking with the `networked` addon.

The browser build is playable at
**[diejor.itch.io/bomber](https://diejor.itch.io/bomber)**.

## What Changed

- Player sessions are managed by `MultiplayerTree`.
- The match scene is spawned through `MultiplayerSceneManager`.
- Player entities use `MultiplayerEntity` based join payloads.
- Web builds use WebRTC with WebTorrent tracker based lobby discovery.
- Desktop builds can also use Steam lobbies when GodotSteam is available.

## Browser Multiplayer

The web version uses `TrackerWebRTCBackend` and `WebTorrentDirectory`.
Players discover rooms through public WebTorrent trackers, then connect with
WebRTC. The trackers are only used for rendezvous. The game traffic is peer to
peer.

You can host and start a match by yourself. To test multiplayer, open the demo
in a second browser tab or share the room with another player.

## Steam

The scene also includes `SteamLobbyDirectory` and Steam backend support for
desktop builds. Steam is optional. If GodotSteam is not available, the web lobby
path still works.

## Running Locally

Open `examples/bomber/main.tscn` in Godot and run the scene. Use the lobby UI to
host a room or join an existing one.

[godot-bomber]: https://github.com/godotengine/godot-demo-projects/tree/3.5-9e68af3/networking/multiplayer_bomber
