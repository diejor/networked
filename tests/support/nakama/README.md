# Local Nakama for live tests

Spins up Nakama + Postgres so the env-gated live transport/storage tests have a
real server to talk to. The offline *shape* tests (API-drift checks) need none
of this — they only require the Nakama addon files to be present.

## Start / stop

```sh
# from repo root
docker compose -f tests/support/nakama/docker-compose.yml up -d     # start
docker compose -f tests/support/nakama/docker-compose.yml ps        # wait for healthy
docker compose -f tests/support/nakama/docker-compose.yml down      # stop (keep data)
docker compose -f tests/support/nakama/docker-compose.yml down -v   # stop + wipe db
```

First boot pulls images and runs the DB migration, so give it ~20-30s until
`nakama` reports `healthy`.

## Endpoints

| Purpose                | Address                  | Notes                         |
|------------------------|--------------------------|-------------------------------|
| Client API + socket    | `127.0.0.1:7350`         | server key `defaultkey`       |
| Developer console      | `http://127.0.0.1:7351`  | login `admin` / `password`    |
| gRPC                   | `127.0.0.1:7349`         | not used by the Godot client  |

These match `NakamaLobbyDirectory` defaults (`server_key=defaultkey`,
`host=127.0.0.1`, `port=7350`, `use_ssl=false`) and `Nakama.gd` `DEFAULT_PORT`.

## Pointing the tests at it

The live tests gate on the `networked/tests/nakama_host` project setting, with the
`NAKAMA_TEST_HOST` environment variable as a fallback. The project setting wins
when both are set. With the container up, set whichever you prefer:

Project setting (persists in `project.godot`, editable under Project Settings ▸
Advanced ▸ Networked ▸ Tests):

```
networked/tests/nakama_host = "127.0.0.1"
```

Environment variable (handy for CI, no commit):

```sh
# bash
export NAKAMA_TEST_HOST=127.0.0.1
```
```powershell
# PowerShell
$env:NAKAMA_TEST_HOST = "127.0.0.1"
```

Leave both empty and the live tests early-return (no-op), so the suite stays
green on machines without Docker. The setting and these suites are
`export-ignore`d, so none of this reaches a game that installs the addon.

Run the live tier directly:

```powershell
godot --path . -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless `
  --ignoreHeadlessMode --ignore-error-breaks -c -rc 1 -rd res://reports `
  -a res://tests/live
```

The default `-a res://tests` run discovers `tests/live/nakama`, but the suite
skips before probing the network when no host is configured.

## Two-client / two-instance testing

`authenticate_device_async` keys an account by device id. Two game instances on
one machine that pass the *same* `device_id` share an account (still distinct
socket presences, which is fine for the relay). For cleaner isolation set a
distinct `NakamaLobbyDirectory.device_id` per instance.
