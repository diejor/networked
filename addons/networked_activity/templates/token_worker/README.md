# Activity token Worker

A Cloudflare Worker that swaps a Discord activity's authorization code with
the client secret. When the request carries a `nonce`, it also signs a token
naming the player, which `DiscordIdToken.signed_by` checks on the host with
the Worker's public key. It takes Discord's own token request, so
`DiscordActivityService.token_url` and `DiscordSocial.token_url` can both
point at it.

## Secrets

```text
CLIENT_SECRET   the Discord application's client secret
SIGNING_KEY     the private key, PKCS#8 PEM
BOT_TOKEN       optional. Checks the player is in the activity instance the
                game names, and adds an instance claim to the token
```

`CLIENT_ID` in `wrangler.toml` is the application ID. `ISSUER` defaults to
`https://<CLIENT_ID>.discordsays.com`.

## Deploy

```sh
cp -r addons/networked_activity/templates/token_worker my-token
cd my-token
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out signing.pem
openssl pkey -in signing.pem -pubout -out activity_token.pub
npx wrangler secret put CLIENT_SECRET
npx wrangler secret put SIGNING_KEY < signing.pem
npx wrangler deploy
```

Copy `activity_token.pub` into the game and keep `signing.pem` out of it. Map
the Worker to `/token` under Activities, URL Mappings, in the Discord
Developer Portal. `node --test` runs its tests.
