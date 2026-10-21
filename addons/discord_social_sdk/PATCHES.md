# Local patches

This directory comes from the
[thiagola92/discord-social-sdk](https://github.com/thiagola92/discord-social-sdk)
release `5.0.0`, file `discord_social_sdk.zip`, folder
`demo/addons/discord_social_sdk/`, under the MIT license in `LICENSE`. It
includes Discord's `discord_partner_sdk` library. Release builds of Networked
leave this directory out. Replay these changes after copying a newer release.

- Only the Linux and Windows libraries are kept. The release has no macOS,
  Android or iOS build.
- `discord_partner_sdk.lib` is left out. It is a Windows import library for
  building the binding and Godot never loads it.
- `discord_krisp.dll` is left out. Discord loads it only after
  `DiscordClient.set_noise_cancellation(true)`, which defaults to off.
