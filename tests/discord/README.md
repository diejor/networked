# Discord suites

These suites name Discord's own classes, so they run only where Discord's
library is installed. Without it `ci/test.py` leaves this folder out and says
so, and the examples play without their Discord panel. The token checks in
`tests/discord_id_token` need no library and always run.

## Turn Discord on

1. Download Discord Social SDK `1.10.19337` from the Discord Developer Portal.
2. Copy `lib/release/libdiscord_partner_sdk.so` from it into
   `addons/discord_social_sdk/lib/linux/`, or
   `bin/release/discord_partner_sdk.dll` into
   `addons/discord_social_sdk/lib/windows/`.
3. Open the editor once. The Networked Discord plugin writes
   `addons/discord_social_sdk/lib/discord.gdextension` and loads it.

Git ignores the library and the `.gdextension`. On Linux, Discord needs glibc
2.38 or newer.

## Run

```sh
python3 ci/test.py gdunit --name tests --path res://tests/
```

A plain gdUnit run of `res://tests` without Discord still loads these suites.
It prints their parse errors and reports nothing for them.
