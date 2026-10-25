# Networked Activity

Runs a Networked web export as a Discord activity. Every player who launches
the activity joins one room, and the first to arrive serves it.

## Install

Copy `addons/networked_activity` with `addons/networked_discord`, and 
`addons/networked_nakama`.  A game whose own WebSocket server serves the 
room leaves out `networked_nakama` and sets `room_transport` and `room_address` 
to reach that server.

## Use

Beside the session add a `NakamaAccount` (the Nakama host, port 443,
`use_ssl`), a `NakamaLobbyDirectory`, a `ConnectBrowser`, and a
`DiscordActivityService` with the activity's `application_id` and its URL
mappings. The host checks each player's Discord account with
`DiscordIdToken.signed_by`.

```sh
python3 ci/export.py web --scene res://game/main.tscn --out build/web
python3 -m http.server -d build/web 8000
```

In the Discord Developer Portal, map the activity's root to that server,
`/nakama` to the Nakama host, and `/token` to a copy of
`templates/token_worker`. `discord_smoke.tscn` logs each step of a launch.
