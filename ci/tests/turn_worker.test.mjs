import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import test from "node:test";

const source = await readFile(new URL(
  "../../addons/networked/templates/turn_worker/index.js", import.meta.url));
const {default: worker} = await import(
  "data:text/javascript;base64," + source.toString("base64"));
const url = "https://turn.example/";
const env = () => ({
  ISSUING: "on", TTL_SECONDS: "86400", ALLOWED_ORIGINS: "*",
  TURN_KEY_ID: "key", TURN_KEY_API_TOKEN: "token",
  TURN_LIMIT: {limit: async () => ({success: true})},
});
const request = (origin, method = "GET") => new Request(url, {
  method, headers: origin ? {Origin: origin} : {},
});
const errorOf = async response => (await response.json()).error;

test("credentials pass through byte for byte with no caching", async t => {
  const body = '{ "iceServers": [{"urls":["turn:relay.example"]}] }\n';
  t.mock.method(globalThis, "fetch", async (address, options) => {
    assert.equal(address, "https://rtc.live.cloudflare.com/v1/turn/keys/" +
      "key/credentials/generate-ice-servers");
    assert.equal(options.headers.Authorization, "Bearer token");
    assert.equal(options.method, "POST");
    assert.deepEqual(JSON.parse(options.body), {ttl: 86400});
    return new Response(body);
  });
  const response = await worker.fetch(request("https://game.example"), env());
  assert.equal(response.status, 200);
  assert.equal(await response.text(), body);
  assert.equal(response.headers.get("Cache-Control"), "no-store");
  assert.equal(response.headers.get("Access-Control-Allow-Origin"),
    "https://game.example");
});

test("preflight and disallowed origins", async () => {
  const settings = {...env(), TURN_KEY_ID: "",
    ALLOWED_ORIGINS: "https://netw-examples.pages.dev," +
      "https://*.netw-examples.pages.dev"};
  for (const origin of ["https://netw-examples.pages.dev",
    "https://branch.netw-examples.pages.dev"]) {
    const response = await worker.fetch(request(origin, "OPTIONS"), settings);
    assert.equal(response.status, 204);
    assert.equal(response.headers.get("Access-Control-Allow-Origin"), origin);
    assert.equal(response.headers.get("Access-Control-Allow-Methods"),
      "GET, OPTIONS");
  }
  for (const origin of ["https://evil.example",
    "https://netw-examples.pages.dev.evil.example",
    "http://branch.netw-examples.pages.dev",
    "https://branch.netw-examples.pages.dev:444", "null"]) {
    const response = await worker.fetch(request(origin), settings);
    assert.equal(response.status, 403);
    assert.equal(response.headers.get("Access-Control-Allow-Origin"), origin);
    assert.equal(await errorOf(response),
      `The origin ${origin} is not allowed. Add it to ALLOWED_ORIGINS.`);
  }
  assert.equal((await worker.fetch(request(), settings)).status, 500);
});

test("missing secrets and invalid lifetimes name the fix", async () => {
  for (const name of ["TURN_KEY_ID", "TURN_KEY_API_TOKEN"]) {
    const response = await worker.fetch(request(), {...env(), [name]: ""});
    assert.equal(response.status, 500);
    assert.equal(await errorOf(response),
      `${name} is not set. Run wrangler secret put ${name}.`);
  }
  for (const TTL_SECONDS of ["0", "172801", "NaN", "1.5"]) {
    const response = await worker.fetch(request(), {...env(), TTL_SECONDS});
    assert.equal(response.status, 500);
    assert.equal(await errorOf(response),
      "TTL_SECONDS is invalid. Set it from 1 to 172800 and deploy.");
  }
});

test("issuing and address limits", async () => {
  const disabled = await worker.fetch(request(), {...env(), ISSUING: "off"});
  assert.equal(disabled.status, 503);
  assert.equal(await errorOf(disabled),
    "Credential issuing is switched off. Set ISSUING to on and deploy.");
  const settings = env();
  settings.TURN_LIMIT.limit = async ({key}) => {
    assert.equal(key, "192.0.2.1");
    return {success: false};
  };
  const response = await worker.fetch(new Request(url, {
    headers: {"CF-Connecting-IP": "192.0.2.1"},
  }), settings);
  assert.equal(response.status, 429);
  assert.equal(response.headers.get("Retry-After"), "60");
  assert.equal(await errorOf(response),
    "Too many credential requests from this address. Try again in a minute.");
});

test("upstream refusal and timeout remain distinct", async t => {
  t.mock.method(globalThis, "fetch", async () => {
    return new Response("", {status: 401});
  });
  const refused = await worker.fetch(request(), env());
  assert.equal(refused.status, 502);
  assert.equal(await errorOf(refused),
    "Cloudflare refused the TURN key with 401. " +
    "Check TURN_KEY_ID and TURN_KEY_API_TOKEN.");
  globalThis.fetch = async () => {
    throw new DOMException("timeout", "TimeoutError");
  };
  const timeout = await worker.fetch(request(), env());
  assert.equal(timeout.status, 504);
  assert.equal(await errorOf(timeout),
    "Cloudflare did not answer within 4 seconds. Try again.");
  globalThis.fetch = async () => { throw new TypeError("network"); };
  const unreachable = await worker.fetch(request(), env());
  assert.equal(unreachable.status, 502);
  assert.equal(await errorOf(unreachable),
    "Cloudflare is unreachable. Check the network and try again.");
});

test("other methods are refused", async () => {
  const response = await worker.fetch(request(undefined, "POST"), env());
  assert.equal(response.status, 405);
  assert.equal(response.headers.get("Allow"), "GET, OPTIONS");
});
