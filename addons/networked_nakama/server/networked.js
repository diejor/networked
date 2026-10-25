// Networked's Nakama JavaScript runtime module.
//
// RPC "networked_join_token" takes {"nonce": "<64 lowercase hex>"} from a
// signed in player and answers {"token": "<JWT>"}, signed RS256. The token
// carries iss "networked-nakama", aud, sub (the Nakama user id), name (display
// name, else username), nonce, iat and exp, which is the expiry of the
// caller's Nakama session. Nothing but the nonce comes from the client.
//
// RPC "networked_party_invite" takes {"user_id", "party_id"} and RPC
// "networked_party_request" takes {"user_id"}. Each sends that user a
// notification from the caller, code 1101 (an invitation to party_id) or 1102
// (the caller asks to join that user's party). NakamaParty sends and reads
// them. Both refuse a user id Nakama does not know. The invite also refuses a
// caller who is not in the party's presence stream, and the request is sent on
// the caller's word, since asking to join needs no membership.
//
// Runtime environment (Nakama's runtime.env):
//   NETWORKED_JOIN_KEY       the private key as a PKCS#8 PEM ("BEGIN PRIVATE
//                            KEY"). Literal \n sequences are read as newlines.
//                            Only the join token needs it.
//   NETWORKED_JOIN_AUDIENCE  optional, default "networked". Games sharing one
//                            Nakama server each set their own.
//
// Install: copy this file into the server's data/modules folder, then
//   openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out join.pem
//   openssl pkey -in join.pem -pubout -out join_token.pub
// Keep join.pem on the server only, and ship join_token.pub with the game for
// NakamaIdToken.

var ISSUER = "networked-nakama";
var DEFAULT_AUDIENCE = "networked";
var NONCE = /^[0-9a-f]{64}$/;
var USER_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
var PARTY_ID = /^[0-9a-f-]{36}\.[A-Za-z0-9_.-]{0,64}$/;
var PARTY_INVITE = 1101;
var PARTY_REQUEST = 1102;
var STREAM_MODE_PARTY = 7;

function parsed(payload, shape) {
  if (!payload || payload.length > 256) {
    throw { message: "Send " + shape + ".", code: 3 };
  }
  try {
    var request = JSON.parse(payload);
    if (request && typeof request === "object") {
      return request;
    }
  } catch (error) {
  }
  throw { message: "Send " + shape + ".", code: 3 };
}

function signedIn(ctx) {
  if (!ctx.userId) {
    throw { message: "Sign in to Nakama first.", code: 16 };
  }
}

function friend(nk, request) {
  if (typeof request.user_id !== "string" || !USER_ID.test(request.user_id)) {
    throw { message: "user_id must be a Nakama user id.", code: 3 };
  }
  if (nk.usersGetId([request.user_id]).length === 0) {
    throw { message: "No Nakama user has that user_id.", code: 5 };
  }
  return request.user_id;
}

function joinToken(ctx, logger, nk, payload) {
  signedIn(ctx);
  var request = parsed(payload, "{\"nonce\": \"<64 hex>\"}");
  if (typeof request.nonce !== "string" || !NONCE.test(request.nonce)) {
    throw { message: "The nonce must be 64 lowercase hex characters.", code: 3 };
  }

  var key = (ctx.env["NETWORKED_JOIN_KEY"] || "").replace(/\\n/g, "\n");
  if (key.indexOf("BEGIN PRIVATE KEY") < 0) {
    logger.error("NETWORKED_JOIN_KEY is missing or is not a PKCS#8 PEM private key.");
    throw { message: "The server has no join token key.", code: 13 };
  }

  var user = nk.accountGetId(ctx.userId).user;
  var now = Math.floor(Date.now() / 1000);
  return JSON.stringify({
    token: nk.jwtGenerate("RS256", key, {
      iss: ISSUER,
      aud: ctx.env["NETWORKED_JOIN_AUDIENCE"] || DEFAULT_AUDIENCE,
      sub: ctx.userId,
      name: user.displayName || user.username,
      nonce: request.nonce,
      iat: now,
      exp: ctx.userSessionExp || now + 300,
    }),
  });
}

function partyInvite(ctx, logger, nk, payload) {
  signedIn(ctx);
  var request = parsed(payload, "{\"user_id\", \"party_id\"}");
  var to = friend(nk, request);
  if (typeof request.party_id !== "string" || !PARTY_ID.test(request.party_id)) {
    throw { message: "party_id must be a Nakama party id.", code: 3 };
  }
  var dot = request.party_id.indexOf(".");
  var present = nk.streamUserList({
    mode: STREAM_MODE_PARTY,
    subject: request.party_id.slice(0, dot),
    label: request.party_id.slice(dot + 1),
  });
  var member = present.some(function (presence) {
    return presence.userId === ctx.userId;
  });
  if (!member) {
    throw { message: "Only a member of the party can invite to it.", code: 7 };
  }
  nk.notificationSend(to, "party invite", { party_id: request.party_id }, PARTY_INVITE, ctx.userId, false);
  return "{}";
}

function partyRequest(ctx, logger, nk, payload) {
  signedIn(ctx);
  var to = friend(nk, parsed(payload, "{\"user_id\"}"));
  nk.notificationSend(to, "party request", {}, PARTY_REQUEST, ctx.userId, false);
  return "{}";
}

function InitModule(ctx, logger, nk, initializer) {
  initializer.registerRpc("networked_join_token", joinToken);
  initializer.registerRpc("networked_party_invite", partyInvite);
  initializer.registerRpc("networked_party_request", partyRequest);
  logger.info("Networked module loaded.");
}
