# Local patches

This directory comes from the
[thiagola92/discord-social-sdk](https://github.com/thiagola92/discord-social-sdk)
release `5.0.0`, file `discord_social_sdk.zip`, folder
`demo/addons/discord_social_sdk/`, under the MIT license in `LICENSE`. The
binding is built against Discord Social SDK `1.10.19337`. Replay these changes
after copying a newer release.

- Discord's own library is left out, because Discord's terms allow shipping it
  only inside a game. That is `lib/linux/libdiscord_partner_sdk.so` and
  `lib/windows/discord_partner_sdk.dll`. Each developer downloads it from the
  Discord Developer Portal, and git ignores it.
- `lib/discord.gdextension` is shipped as `lib/discord.gdextension.template`.
  The Networked Discord editor plugin copies it to `lib/discord.gdextension`
  once Discord's library is in place, so Godot never loads the binding without
  it. Git ignores the copy.
- Only the Linux and Windows libraries are kept. The release has no macOS,
  Android or iOS build.
- `discord_partner_sdk.lib` is left out. It is a Windows import library for
  building the binding and Godot never loads it.
- `discord_krisp.dll` is left out. Discord loads it only after
  `DiscordClient.set_noise_cancellation(true)`, which defaults to off.
