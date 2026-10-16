const UPSTREAM_TIMEOUT_MS = 4000;
const TURN_ENDPOINT = "https://rtc.live.cloudflare.com/v1/turn/keys/";

export default {
  async fetch(request, env) {
    const origin = request.headers.get("Origin") || "";
    const allowed = (env.ALLOWED_ORIGINS || "").split(",").map(x => x.trim());
    const accepts = !origin || allowed.some(pattern => {
      if (pattern === "*" || pattern === origin) return true;
      if (!pattern.startsWith("https://*.")) return false;
      let url;
      try { url = new URL(origin); } catch { return false; }
      const suffix = pattern.slice("https://*".length);
      return url.protocol === "https:" && url.port === "" &&
        url.origin === origin && url.hostname.endsWith(suffix);
    });
    const headers = {
      "Content-Type": "application/json",
      "Cache-Control": "no-store",
      "Vary": "Origin",
      "Access-Control-Allow-Methods": "GET, OPTIONS",
    };
    if (origin) headers["Access-Control-Allow-Origin"] = origin;
    const refuse = (status, error, extra = {}) => new Response(
      JSON.stringify({error}), {status, headers: {...headers, ...extra}}
    );
    if (!accepts) return refuse(403,
      `The origin ${origin} is not allowed. Add it to ALLOWED_ORIGINS.`);
    if (request.method === "OPTIONS") {
      return new Response(null, {status: 204, headers});
    }
    if (request.method !== "GET") return refuse(405,
      "The credential endpoint accepts GET. Send a GET request.",
      {Allow: "GET, OPTIONS"});
    if (env.ISSUING !== "on") return refuse(503,
      "Credential issuing is switched off. Set ISSUING to on and deploy.");
    for (const name of ["TURN_KEY_ID", "TURN_KEY_API_TOKEN"]) {
      if (!env[name]) return refuse(500,
        `${name} is not set. Run wrangler secret put ${name}.`);
    }
    const ttl = Number(env.TTL_SECONDS);
    if (!Number.isInteger(ttl) || ttl <= 0 || ttl > 172800) {
      return refuse(500,
        "TTL_SECONDS is invalid. Set it from 1 to 172800 and deploy.");
    }
    const {success} = await env.TURN_LIMIT.limit({
      key: request.headers.get("CF-Connecting-IP") || "unknown",
    });
    if (!success) return refuse(429,
      "Too many credential requests from this address. Try again in a minute.",
      {"Retry-After": "60"});
    try {
      const upstream = await fetch(
        TURN_ENDPOINT + encodeURIComponent(env.TURN_KEY_ID) +
          "/credentials/generate-ice-servers", {
          method: "POST",
          headers: {
            Authorization: `Bearer ${env.TURN_KEY_API_TOKEN}`,
            "Content-Type": "application/json",
          },
          body: JSON.stringify({ttl}),
          signal: AbortSignal.timeout(UPSTREAM_TIMEOUT_MS),
        });
      if (!upstream.ok) return refuse(502,
        `Cloudflare refused the TURN key with ${upstream.status}. ` +
        "Check TURN_KEY_ID and TURN_KEY_API_TOKEN.");
      const body = await upstream.arrayBuffer();
      return new Response(body, {status: 200, headers});
    } catch (error) {
      if (error.name === "TimeoutError" || error.name === "AbortError") {
        return refuse(504,
          "Cloudflare did not answer within 4 seconds. Try again.");
      }
      return refuse(502,
        "Cloudflare is unreachable. Check the network and try again.");
    }
  },
};
