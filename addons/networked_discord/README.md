# Networked Discord

Discord presence, friends, parties with voice and sign in for a Networked
session, using Discord's Social SDK.

## Install

1. Unzip `networked_discord` from the Networked release into your project's
   root, beside `networked`. It holds `addons/networked_discord` and
   `addons/discord_social_sdk`. The second is the
   [Discord Social SDK binding](https://github.com/thiagola92/discord-social-sdk)
   for Linux and Windows.
2. Download Discord Social SDK `1.10.19337` from the
   [Discord Developer Portal](https://discord.com/developers/applications).
   Copy its `lib/release/libdiscord_partner_sdk.so` into
   `addons/discord_social_sdk/lib/linux/` and its
   `bin/release/discord_partner_sdk.dll` into
   `addons/discord_social_sdk/lib/windows/`.
3. Enable Networked Discord under Project Settings, Plugins. It turns the
   binding on once Discord's library is in place.
4. In your Discord application, enable the Social SDK. `sign_in` and `DiscordIdToken`
   need Public Client turned on under OAuth2 and the redirect
   `http://127.0.0.1/callback`.

Your game includes Discord's library and owns Discord's terms. It keeps out
players under 13, meets Discord's integration requirements, and deletes a
player's Discord data when they ask.

A web export leaves out the Discord nodes and keeps `DiscordIdToken`.

## Use

Add a `DiscordSocial` node and set its `application_id`.
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

The panel's font is Figtree under the SIL Open Font License
(`ui/fonts/OFL.txt`).
