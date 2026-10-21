# Networked Discord

Discord presence, friends, parties with voice and sign in for a Networked
session, using Discord's Social SDK.

## Install

1. Install "Discord Social SDK" from the Asset Library, or from its
   [GitHub release](https://github.com/thiagola92/discord-social-sdk/releases).
   It includes Discord's library.
2. Enable Networked Discord under Project Settings, Plugins.
3. In your Discord application, enable the Social SDK. `sign_in` and `prove`
   need Public Client turned on under OAuth2 and the redirect
   `http://127.0.0.1/callback`.

A web export leaves out the Discord nodes and keeps `DiscordIdToken`.

## Use

Add a `DiscordSocial` node beside the session and set its `application_id`.
Add `res://addons/networked_discord/ui/discord_panel.tscn` for a ready made
friend list, party and invitations.

```gdscript
@onready var session: NetwSession = Netw.session(self)
@onready var discord: DiscordSocial = session.service(DiscordSocial)

func _ready() -> void:
    discord.details = "Bomber"
    session.connection.invited.connect(_on_invited)

func _on_invited(endpoint: NetwEndpoint) -> void:
    await session.connection.join(endpoint).settled
```

A game that keeps a `ConnectBrowser` lets it answer invites, or sets its
`join_invites` to `false` before joining from its own handler, so the player
joins once.

Open `DiscordSocial` in the editor help (F1) for friends, parties, voice and
proving a player's Discord account to the server.

The panel's font is Figtree under the SIL Open Font License
(`ui/fonts/OFL.txt`).
