import { test, beforeEach } from "node:test";
import assert from "node:assert/strict";
import worker from "../src/index.js";

const APP = "1519144648411447338";
const USER = { id: "205519959982473217", username: "ana", global_name: "Ana" };

const pair = await crypto.subtle.generateKey(
  {
    name: "RSASSA-PKCS1-v1_5",
    modulusLength: 2048,
    publicExponent: new Uint8Array([1, 0, 1]),
    hash: "SHA-256",
  },
  true,
  ["sign", "verify"],
);
const pkcs8 = btoa(
  String.fromCharCode(
    ...new Uint8Array(await crypto.subtle.exportKey("pkcs8", pair.privateKey)),
  ),
);
const env = {
  CLIENT_ID: APP,
  CLIENT_SECRET: "secret",
  SIGNING_KEY:
    "-----BEGIN PRIVATE KEY-----\n" +
    pkcs8.match(/.{1,64}/g).join("\n") +
    "\n-----END PRIVATE KEY-----\n",
};

let calls;
let instanceUsers;

beforeEach(() => {
  calls = [];
  instanceUsers = [USER.id];
  globalThis.fetch = async (url, init = {}) => {
    calls.push({ url: String(url), init });
    if (String(url).endsWith("/oauth2/token")) {
      return Response.json({ access_token: "access", token_type: "Bearer" });
    }
    if (String(url).endsWith("/users/@me")) {
      return Response.json(USER);
    }
    return Response.json({ users: instanceUsers });
  };
});

const ask = (fields, method = "POST") =>
  worker.fetch(
    new Request("https://worker.test/token", {
      method,
      body: method === "POST" ? new URLSearchParams(fields) : undefined,
    }),
    env,
  );

const decoded = (part) =>
  JSON.parse(atob(part.replace(/-/g, "+").replace(/_/g, "/")));

async function verified(token) {
  const [header, claims, signature] = token.split(".");
  const ok = await crypto.subtle.verify(
    "RSASSA-PKCS1-v1_5",
    pair.publicKey,
    Uint8Array.from(
      atob(signature.replace(/-/g, "+").replace(/_/g, "/")),
      (c) => c.charCodeAt(0),
    ),
    new TextEncoder().encode(header + "." + claims),
  );
  assert.ok(ok, "the signature does not verify");
  return { header: decoded(header), claims: decoded(claims) };
}

test("a code swap without a nonce answers Discord's grant", async () => {
  const answer = await ask({ grant_type: "authorization_code", code: "c" });
  assert.equal(answer.status, 200);
  assert.deepEqual(await answer.json(), {
    access_token: "access",
    token_type: "Bearer",
  });
  const sent = new URLSearchParams(calls[0].init.body);
  assert.equal(sent.get("client_secret"), "secret");
  assert.equal(sent.get("code"), "c");
  assert.equal(calls.length, 1);
});

test("a nonce gets an RS256 token naming the player", async () => {
  const answer = await ask({
    grant_type: "authorization_code",
    code: "c",
    nonce: "n0nce",
    code_verifier: "v",
  });
  const body = await answer.json();
  assert.equal(body.access_token, "access");
  const { header, claims } = await verified(body.id_token);
  assert.equal(header.alg, "RS256");
  assert.equal(claims.iss, `https://${APP}.discordsays.com`);
  assert.equal(claims.aud, APP);
  assert.equal(claims.sub, USER.id);
  assert.equal(claims.name, "Ana");
  assert.equal(claims.nonce, "n0nce");
  assert.equal(claims.exp - claims.iat, 3600);
  assert.equal(claims.instance, undefined);
  assert.equal(new URLSearchParams(calls[0].init.body).get("code_verifier"), "v");
});

test("a bot token checks the activity instance", async () => {
  const withBot = { ...env, BOT_TOKEN: "bot" };
  const fields = {
    grant_type: "authorization_code",
    code: "c",
    nonce: "n",
    instance_id: "i-1-gc-2-3",
  };
  const ask_with_bot = () =>
    worker.fetch(
      new Request("https://worker.test/token", {
        method: "POST",
        body: new URLSearchParams(fields),
      }),
      withBot,
    );

  const admitted = await ask_with_bot();
  const { claims } = await verified((await admitted.json()).id_token);
  assert.equal(claims.instance, "i-1-gc-2-3");

  instanceUsers = ["someone else"];
  const refused = await ask_with_bot();
  assert.equal(refused.status, 403);
});

test("a bad request is refused before Discord is asked", async () => {
  for (const answer of [
    await ask({ grant_type: "authorization_code" }),
    await ask({ grant_type: "client_credentials", code: "c" }),
    await ask({ grant_type: "refresh_token", code: "c" }),
    await ask({}, "GET"),
  ]) {
    assert.ok(answer.status >= 400);
  }
  assert.equal(calls.length, 0);
});
