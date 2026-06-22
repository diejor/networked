# Nakama Transport Notes

Live Nakama coverage lives under `tests/live/nakama`. It is opt-in through
`NAKAMA_TEST_HOST` and uses the local Docker stack in
`tests/support/nakama/docker-compose.yml`.

As of June 22, 2026, the live peer-awareness suite records the relay behavior
this addon depends on: `server_relay` can stay disabled because
`NakamaMultiplayerBridge` manufactures Godot peer awareness for all match
participants. The live tests assert that three in-process participants see each
other through `MultiplayerAPI.get_peers()`, keep matching rosters through
`MultiplayerTree.get_joined_players()`, and receive disconnect propagation.

The quick-start replication suite adds one scene-level proof over the same
relay: players spawn on all peers, host-authored state reaches clients, and
client input reaches server authority.
