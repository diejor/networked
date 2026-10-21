## Checks the proof a joining player sends from [method DiscordSocial.prove].
##
## [method admit] accepts a join when the proof holds a valid Discord ID
## token and the key that token names signed the server's challenge. It
## trusts only Discord's own signing keys. Call [method fetch_keys] before
## hosting so the first join does not wait for them.
## [codeblock]
## func _init() -> void:
##     Netw.configure_admission(admit).proof(prove)
##
## func prove(challenge: String) -> PackedByteArray:
##     return await discord.prove(challenge)
##
## func admit(request: NetwJoinRequest) -> Error:
##     return await DiscordIdToken.admit(request, APPLICATION_ID)
## [/codeblock]
class_name DiscordIdToken
extends RefCounted

## How far a token's times may differ from this machine's clock, in seconds.
const CLOCK_SKEW_SECONDS := 60

const _KEY_LIFETIME_MSEC := 3600000
const _FETCH_COOLDOWN_MSEC := 30000
const _MAX_PROOF_BYTES := 20480
const _MAX_KEY_LENGTH := 2048
const _MAX_SIGNATURE_BYTES := 1024
const _KEYS_URL := "https://discord.com/api/oauth2/keys"
const _FETCH_TIMEOUT_SECONDS := 10.0
const _MAX_RESPONSE_BYTES := 65536
const _BASE64URL := "(?:[A-Za-z0-9_-]{4})*[A-Za-z0-9_-]{2,4}"

static var _keys: Dictionary = { }
static var _expires_at: int = 0
static var _retry_at: int = 0
static var _pending: NetwPromise


## Returns [constant @GlobalScope.OK] when [member NetwJoinRequest.proof]
## answers [member NetwJoinRequest.challenge] for [param application_id],
## and [constant @GlobalScope.ERR_UNAUTHORIZED] otherwise. Call it with
## [code]await[/code] from the admission handler.
## [br][br]
## On success it sets [member NetwJoinRequest.account] to
## [code]"discord:"[/code] and the user ID, and
## [member NetwJoinRequest.account_name] to the signed display name, which
## may be empty. It may wait for one [method fetch_keys], which counts
## against [member NetwMultiplayer.auth_timeout].
## [br][br]
## It also accepts a challenge that is not bound to this server, as over
## ENet, where another server of this game could pass the challenge to its
## own player and join here as that player's Discord account.
static func admit(request: NetwJoinRequest, application_id: int) -> Error:
	var proof: Variant = request.proof
	var token := ""
	var key_text := ""
	var signature := PackedByteArray()
	if (
			proof is PackedByteArray
			and proof.size() >= 4 and proof.size() <= _MAX_PROOF_BYTES
	):
		match proof.decode_var(0):
			[var t, var k, var s] when (
					t is String and k is String and s is PackedByteArray
					and k.length() <= _MAX_KEY_LENGTH
					and s.size() <= _MAX_SIGNATURE_BYTES
			):
				token = t
				key_text = k
				signature = s
	var challenge := request.challenge
	if challenge.is_empty() or application_id <= 0 or key_text.is_empty():
		return ERR_UNAUTHORIZED
	var shape := "^%s\\.%s\\.%s\\z" % [_BASE64URL, _BASE64URL, _BASE64URL]
	if RegEx.create_from_string(shape).search(token) == null:
		return ERR_UNAUTHORIZED

	var parts := token.split(".")
	assert(parts.size() == 3)
	var decoded: Array[PackedByteArray] = []
	for part in parts:
		var padded := part.replace("-", "+").replace("_", "/")
		padded += "=".repeat(posmod(-padded.length(), 4))
		decoded.append(Marshalls.base64_to_raw(padded))
	var json := JSON.new()
	if json.parse(decoded[0].get_string_from_utf8()) != OK:
		return ERR_UNAUTHORIZED
	var header: Variant = json.data
	if json.parse(decoded[1].get_string_from_utf8()) != OK:
		return ERR_UNAUTHORIZED
	var claims: Variant = json.data
	if not claims is Dictionary:
		return ERR_UNAUTHORIZED

	var kid := ""
	match header:
		{ "crit", .. }, { "b64", .. }:
			return ERR_UNAUTHORIZED
		{ "alg": "RS256", "kid": var id, .. } when id is String:
			kid = id
	if kid.is_empty():
		return ERR_UNAUTHORIZED

	var audience := str(application_id)
	var nonce := key_text.sha256_text()
	var account := ""
	var account_name := ""
	match { "azp": audience, "name": "" }.merged(claims, true):
		{
			"iss": "https://discord.com",
			"nonce": nonce,
			"aud": var aud,
			"azp": audience,
			"sub": var sub,
			"name": var name,
			..
		} when (
				aud in [audience, [audience]]
				and sub is String and not sub.is_empty()
				and name is String
		):
			account = "discord:" + sub
			account_name = name
	if account.is_empty():
		return ERR_UNAUTHORIZED

	var expiry: Variant = claims.get("exp")
	var issued: Variant = claims.get("iat")
	var not_before: Variant = claims.get("nbf", issued)
	for time in [expiry, issued, not_before]:
		if not time is float or not is_finite(time):
			return ERR_UNAUTHORIZED
	var now := Time.get_unix_time_from_system()
	if expiry + CLOCK_SKEW_SECONDS <= now or issued >= expiry:
		return ERR_UNAUTHORIZED
	if maxf(issued, not_before) > now + CLOCK_SKEW_SECONDS:
		return ERR_UNAUTHORIZED

	var stale := not _keys.has(kid) or Time.get_ticks_msec() >= _expires_at
	if stale and Time.get_ticks_msec() >= _retry_at:
		await fetch_keys().wait()
	if not _keys.has(kid) or Time.get_ticks_msec() >= _expires_at:
		_retry_at = Time.get_ticks_msec() + _FETCH_COOLDOWN_MSEC
		return ERR_UNAUTHORIZED

	var crypto := Crypto.new()
	var message := parts[0] + "." + parts[1]
	if not crypto.verify(
		HashingContext.HASH_SHA256,
		message.sha256_buffer(),
		decoded[2],
		_keys[kid],
	):
		return ERR_UNAUTHORIZED

	var key := CryptoKey.new()
	if key.load_from_string(key_text, true) != OK:
		return ERR_UNAUTHORIZED
	if not crypto.verify(
		HashingContext.HASH_SHA256,
		challenge.sha256_buffer(),
		signature,
		key,
	):
		return ERR_UNAUTHORIZED

	request.account = account
	request.account_name = account_name
	return OK


## Downloads Discord's signing keys and resolves with
## [constant @GlobalScope.OK]. A call while a download is pending returns
## the same [NetwPromise]. A failed download keeps the previous keys.
static func fetch_keys() -> NetwPromise:
	if _pending != null:
		return _pending
	var tree := Engine.get_main_loop() as SceneTree
	if tree == null:
		return NetwPromise.rejected(
			ERR_UNCONFIGURED,
			"Fetching Discord signing keys needs a running SceneTree.",
		)

	var request := HTTPRequest.new()
	request.timeout = _FETCH_TIMEOUT_SECONDS
	request.body_size_limit = _MAX_RESPONSE_BYTES
	request.max_redirects = 0
	tree.root.add_child(request)
	request.request_completed.connect(
		_on_request_completed.bind(request),
		CONNECT_ONE_SHOT,
	)

	_pending = NetwPromise.new()
	var promise := _pending
	if request.request(_KEYS_URL) != OK:
		_on_request_completed(
			HTTPRequest.RESULT_CANT_CONNECT,
			0,
			PackedStringArray(),
			PackedByteArray(),
			request,
		)
	return promise


static func _on_request_completed(
		result: int,
		status: int,
		_headers: PackedStringArray,
		body: PackedByteArray,
		request: HTTPRequest,
) -> void:
	request.queue_free()
	var promise := _pending
	assert(promise != null and not promise.is_settled)
	_pending = null

	var json := JSON.new()
	var listed: Array = []
	if (
			result == HTTPRequest.RESULT_SUCCESS and status == 200
			and body.size() <= _MAX_RESPONSE_BYTES
			and json.parse(body.get_string_from_utf8()) == OK
	):
		match json.data:
			{ "keys": var found, .. } when found is Array:
				listed = found

	var shape := RegEx.create_from_string("^%s\\z" % _BASE64URL)
	var defaults := { "alg": "RS256", "use": "sig", "key_ops": ["verify"] }
	var keys: Dictionary = { }
	for jwk: Variant in listed:
		var kid := ""
		var modulus := ""
		match defaults.merged(jwk, true) if jwk is Dictionary else { }:
			{
				"kty": "RSA",
				"alg": "RS256",
				"use": "sig",
				"key_ops": ["verify"],
				"e": "AQAB",
				"kid": var id,
				"n": var encoded,
				..
			} when id is String and encoded is String:
				kid = id
				modulus = encoded
		if kid.is_empty() or shape.search(modulus) == null:
			continue

		var padded := modulus.replace("-", "+").replace("_", "/")
		padded += "=".repeat(posmod(-padded.length(), 4))
		var n := Marshalls.base64_to_raw(padded)
		if n.size() < 256 or n.size() > 1024 or n[0] < 128 or n[-1] % 2 == 0:
			continue

		var lengths := [n.size() + 10, n.size() + 1]
		var der := ("3082%04x0282%04x00" % lengths).hex_decode()
		der += n + "0203010001".hex_decode()
		assert(
			der.size() == lengths[0] + 4,
			"The DER sequence length counts every byte after its header.",
		)
		var pem := "-----BEGIN RSA PUBLIC KEY-----\n"
		pem += Marshalls.raw_to_base64(der)
		pem += "\n-----END RSA PUBLIC KEY-----\n"
		var key := CryptoKey.new()
		if keys.has(kid) or key.load_from_string(pem, true) != OK:
			keys.clear()
			break
		keys[kid] = key

	if keys.is_empty():
		promise.reject(
			ERR_CANT_CONNECT,
			"Discord signing keys could not be downloaded. Check that this "
			+ "machine can reach discord.com.",
		)
		return
	_keys = keys
	_expires_at = Time.get_ticks_msec() + _KEY_LIFETIME_MSEC
	_retry_at = 0
	promise.resolve(OK)
