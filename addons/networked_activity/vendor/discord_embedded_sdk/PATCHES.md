# Local patches

`DiscordSDK.gd` comes from
[kuylar/discord-embedded-godot](https://github.com/kuylar/discord-embedded-godot),
file `plugins/DiscordEmbeddedSDK/DiscordSDK.gd`, under the MIT license in
`LICENSE`. Replay these changes after copying a newer upstream file.

- `subscribe_event(event)` is split out of `subscribe_to_events()`. The
  service subscribes only to the events its OAuth scopes allow, because
  Discord answers an unscoped subscription with an error.
- The inner `class DiscordActivity` is renamed `DiscordActivityInfo`. The
  upstream name hides the Discord Social SDK's global `DiscordActivity`.
- The inner `class DiscordParty` is renamed `DiscordPartyInfo`. The upstream
  name hides the global `DiscordParty` in `addons/networked_discord`.
- `_handle_message` drops a message whose origin is not in `ALLOWED_ORIGINS`
  or the page's own origin. The list is the one in Discord's
  `@discord/embedded-app-sdk`, `src/Discord.ts`.
- `sendMessage` posts to `source_origin`, the referrer or `'*'` without one,
  as Discord's SDK does. Upstream always posts to `'*'`.
