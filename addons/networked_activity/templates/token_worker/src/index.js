const DISCORD_API = "https://discord.com/api";
const TOKEN_LIFETIME_SECONDS = 3600;
const GRANTS = { authorization_code: "code", refresh_token: "refresh_token" };
const FORWARDED = [
  "grant_type",
  "code",
  "redirect_uri",
  "code_verifier",
  "refresh_token",
];

const base64url = (bytes) =>
  btoa(String.fromCharCode(...new Uint8Array(bytes)))
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");

const encoded = (value) =>
  base64url(new TextEncoder().encode(JSON.stringify(value)));

const refuse = (status, error) => Response.json({ error }, { status });

async function sign(claims, pem) {
  const der = Uint8Array.from(
    atob(pem.replace(/-----[^-]+-----/g, "").replace(/\s+/g, "")),
    (c) => c.charCodeAt(0),
  );
  const key = await crypto.subtle.importKey(
    "pkcs8",
    der,
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const body = encoded({ alg: "RS256", typ: "JWT" }) + "." + encoded(claims);
  const signature = await crypto.subtle.sign(
    "RSASSA-PKCS1-v1_5",
    key,
    new TextEncoder().encode(body),
  );
  return body + "." + base64url(signature);
}

export default {
  async fetch(request, env) {
    if (request.method !== "POST") {
      return refuse(405, "post a token request");
    }
    const asked = new URLSearchParams(await request.text());
    const needed = GRANTS[asked.get("grant_type")];
    if (!needed || !asked.get(needed)) {
      return refuse(400, "send grant_type with its code or refresh_token");
    }

    const swap = new URLSearchParams({
      client_id: env.CLIENT_ID,
      client_secret: env.CLIENT_SECRET,
    });
    for (const field of FORWARDED) {
      if (asked.has(field)) {
        swap.set(field, asked.get(field));
      }
    }
    const swapped = await fetch(DISCORD_API + "/oauth2/token", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: swap,
    });
    const nonce = asked.get("nonce");
    if (!swapped.ok || !nonce) {
      return new Response(swapped.body, {
        status: swapped.status,
        headers: { "Content-Type": "application/json" },
      });
    }

    const grant = await swapped.json();
    const me = await fetch(DISCORD_API + "/users/@me", {
      headers: { Authorization: "Bearer " + grant.access_token },
    });
    if (!me.ok) {
      return refuse(502, "Discord did not answer /users/@me");
    }
    const user = await me.json();

    const now = Math.floor(Date.now() / 1000);
    const claims = {
      iss: env.ISSUER || `https://${env.CLIENT_ID}.discordsays.com`,
      aud: env.CLIENT_ID,
      sub: user.id,
      name: user.global_name || user.username,
      nonce,
      iat: now,
      exp: now + TOKEN_LIFETIME_SECONDS,
    };
    const instance = asked.get("instance_id");
    if (env.BOT_TOKEN && instance) {
      const found = await fetch(
        `${DISCORD_API}/applications/${env.CLIENT_ID}/activity-instances/${encodeURIComponent(instance)}`,
        { headers: { Authorization: "Bot " + env.BOT_TOKEN } },
      );
      const listed = found.ok ? await found.json() : { users: [] };
      if (!(listed.users || []).includes(user.id)) {
        return refuse(403, "the player is not in that activity instance");
      }
      claims.instance = instance;
    }

    return Response.json({
      ...grant,
      id_token: await sign(claims, env.SIGNING_KEY),
    });
  },
};
