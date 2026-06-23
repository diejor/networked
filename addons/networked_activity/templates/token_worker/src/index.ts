// Discord Activity OAuth token Worker.
//
// Exchanges the OAuth `code` the embedded client gets from
// DiscordSDK.command_authorize() for an `access_token`, which the client then
// passes to DiscordSDK.command_authenticate(). The CLIENT_SECRET never leaves
// this Worker. This is the v1 (client-claimed) identity path; verified identity
// is future work (a Nakama runtime module or the dedicated WSS server).
//
// Deploy with `wrangler deploy`. Set the secret with
// `wrangler secret put CLIENT_SECRET`. Map this Worker to the `/token` (or
// `/.proxy/token`) prefix in the Discord dev portal URL mappings.
import { Hono } from "hono";

type Bindings = {
  CLIENT_ID: string;
  CLIENT_SECRET: string;
};

const app = new Hono<{ Bindings: Bindings }>();

app.get("/", (c) => c.text("networked_activity token worker"));

app.post("/token", async (c) => {
  const code = await c.req
    .json<{ code?: unknown }>()
    .then((b) => b.code)
    .catch(() => undefined);

  if (typeof code !== "string" || code.length === 0) {
    return c.json({ error: "missing or invalid code" }, 400);
  }

  const response = await fetch("https://discord.com/api/oauth2/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      client_id: c.env.CLIENT_ID,
      client_secret: c.env.CLIENT_SECRET,
      grant_type: "authorization_code",
      code,
    }),
  });

  if (!response.ok) {
    console.error({ status: response.status, details: response.statusText });
    return c.json({ error: "token exchange failed" }, 502);
  }

  const { access_token } = (await response.json()) as { access_token: string };
  return c.json({ access_token });
});

export default app;
