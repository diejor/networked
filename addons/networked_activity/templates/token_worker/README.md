# Token Worker (Discord Activity identity, v1)

A ~50-line Hono Worker that exchanges the OAuth `code` from
`DiscordSDK.command_authorize()` for an `access_token`. The client then calls
`DiscordSDK.command_authenticate(access_token)` to resolve the local
`DiscordUser`. The `CLIENT_SECRET` never reaches the client.

This is the **client-claimed** identity path. It is fine for friendly play but
is **not spoof-proof**: in the Nakama listen-server topology "peer 1" is another
player's browser and cannot hold a bot token. Verified identity (a Nakama
runtime module or the dedicated WSS server doing `get_activity_instance`
verification) is tracked as Track B.

## Deploy

```sh
cp -r <addon>/templates/token_worker my-game-token
cd my-game-token
npm install
# CLIENT_ID is public; put it in wrangler.toml [vars].
# CLIENT_SECRET is a secret; never commit it.
npx wrangler secret put CLIENT_SECRET
npx wrangler deploy
```

## Discord dev portal

Map this Worker behind a URL prefix (Activities → URL Mappings), e.g. `/token`
or `/.proxy/token`, and point `DiscordActivityService.token_endpoint` at the
same path. The client posts `{ "code": "..." }` and reads `{ "access_token":
"..." }` back.
