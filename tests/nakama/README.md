# Local Nakama for live tests

Spins up Nakama + Postgres so the env-gated live transport/storage tests have a
real server to talk to. The offline *shape* tests (API-drift checks) need none
of this — they only require the Nakama addon files to be present.

## Start / stop

```sh
# from repo root
docker compose -f tests/nakama/docker-compose.yml up -d     # start
docker compose -f tests/nakama/docker-compose.yml ps        # wait for healthy
docker compose -f tests/nakama/docker-compose.yml down      # stop (keep data)
docker compose -f tests/nakama/docker-compose.yml down -v   # stop + wipe db
```

First boot pulls images and runs the DB migration, so give it ~20-30s until
`nakama` reports `healthy`.

## Endpoints

| Purpose                | Address                  | Notes                         |
|------------------------|--------------------------|-------------------------------|
| Client API + socket    | `127.0.0.1:7350`         | server key `defaultkey`       |
| Developer console      | `http://127.0.0.1:7351`  | login `admin` / `password`    |
| gRPC                   | `127.0.0.1:7349`         | not used by the Godot client  |

These match `NakamaAccount` defaults (`server_key=defaultkey`,
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

Leave both empty and the live tests are skipped.

Run the live tests directly:

```sh
godot --path . -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless \
  --ignoreHeadlessMode --ignore-error-breaks -c -rc 1 -rd res://reports \
  -a res://tests/nakama
```

The join token suite needs its own server with the module loaded. Start it with
`docker compose -p netwjointoken -f tests/nakama/join_token/docker-compose.yml up -d`,
run `res://tests/nakama/test_nakama_join_token.gd` with
`NAKAMA_JOIN_TOKEN_HOST=127.0.0.1`, then `down -v` the same project.

## Two-client / two-instance testing

A `NakamaAccount` that nobody signed in takes the first free guest slot,
`user://nakama_guest_<n>`, so each running instance is its own Nakama user. In
one process every account shares a guest. The suites sign each tree in as its
own user through
`NakamaTestSupport.sign_in`, which hands a session to
`NakamaAccount.use_session`.
