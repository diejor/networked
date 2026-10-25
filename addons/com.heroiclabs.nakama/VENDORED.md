# Vendored Nakama Godot SDK

This folder is `addons/com.heroiclabs.nakama` from
[heroiclabs/nakama-godot](https://github.com/heroiclabs/nakama-godot) at
commit `6dc7b3951d98cc3807cafb3f1dbc9b44d9b98d21` (2026-09-22), copied
unchanged except that `Satori`, `Satori.gd` and `dotnet-utils` are removed.
The `.uid` files of scripts that already existed here are kept.

Upstream has cut no release since 3.4.0. Against 3.4.0 this commit adds
`Nakama.get_device_id()`, a random id kept in `user://nakama_device_id`,
caps the total time of a request across its retries, adds type hints, fixes
a leak of finished HTTP requests and refuses a storage id of the wrong type
instead of skipping it.
