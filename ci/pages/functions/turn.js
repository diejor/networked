const JSON_HEADERS = {
  "content-type": "application/json; charset=utf-8",
  "cache-control": "no-store",
};

function answer(servers) {
  return new Response(JSON.stringify(servers), {
    status: 200,
    headers: JSON_HEADERS,
  });
}

function splitList(text) {
  return String(text)
    .split(",")
    .map((piece) => piece.trim())
    .filter((piece) => piece.length > 0);
}

function configuredServers(env) {
  const urls = splitList(env.TURN_URLS || "");
  if (urls.length === 0) {
    return [];
  }
  const server = { urls };
  if (env.TURN_USERNAME) {
    server.username = env.TURN_USERNAME;
  }
  if (env.TURN_CREDENTIAL) {
    server.credential = env.TURN_CREDENTIAL;
  }
  return [server];
}

async function upstreamServers(env) {
  const headers = {};
  if (env.TURN_UPSTREAM_HEADER && env.TURN_UPSTREAM_TOKEN) {
    headers[env.TURN_UPSTREAM_HEADER] = env.TURN_UPSTREAM_TOKEN;
  }
  const relayed = await fetch(env.TURN_UPSTREAM_URL, {
    method: "GET",
    headers,
    signal: AbortSignal.timeout(5000),
  });
  if (!relayed.ok) {
    throw new Error("upstream answered " + relayed.status);
  }
  const parsed = await relayed.json();
  if (!Array.isArray(parsed)) {
    throw new Error("upstream answered no array");
  }
  return parsed.filter(
    (server) =>
      server !== null &&
      typeof server === "object" &&
      Object.prototype.hasOwnProperty.call(server, "urls"),
  );
}

export async function onRequest({ request, env }) {
  if (request.method !== "GET") {
    return new Response(JSON.stringify({ error: "Method Not Allowed" }), {
      status: 405,
      headers: { ...JSON_HEADERS, allow: "GET" },
    });
  }
  if (env.TURN_UPSTREAM_URL) {
    try {
      return answer(await upstreamServers(env));
    } catch (reason) {
      console.log("turn upstream refused: " + reason.message);
      return answer(configuredServers(env));
    }
  }
  return answer(configuredServers(env));
}
