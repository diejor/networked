# Networked Steam

Steam lobbies, friends, invites, parties and sign in for a Networked session,
using [GodotSteam](https://codeberg.org/godotsteam/godotsteam).

## Install

Unzip `networked_steam` from the Networked release into your project's root,
beside `networked`. It holds `addons/networked_steam` and `addons/godotsteam`,
which is GodotSteam for Linux, Windows and macOS.

Set `steam/initialization/app_id` in the project settings to your Steam app
id. GodotSteam adds that setting.

## Use

Add a `SteamSocial` node for friends and invites, and a `SteamLobbyDirectory`
to host and join Steam lobbies.

```gdscript
@onready var session: NetwSession = Netw.session(self)
@onready var steam: SteamSocial = session.service(SteamSocial)

func _ready() -> void:
    session.connection.invited.connect(_on_invited)

func _on_invited(endpoint: NetwEndpoint) -> void:
    await session.connection.join(endpoint).settled
```

`SteamTicket` proves a player's Steam account to the server through
`Netw.configure_admission`.
