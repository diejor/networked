extends GdUnitTestSuite

const APPLICATION_ID := 1561065725962092666
const SUBJECT := "123456789012345678"
const ACCEPTED := [OK, "discord:" + SUBJECT, ""]
const REFUSED := [ERR_UNAUTHORIZED, "", ""]
const TOKENS := "res://tests/discord_id_token/fixtures/tokens.json"


class Gate extends Node:
	var proof_of: Callable
	var outcomes: Array[Array] = []


	func prove(challenge: String) -> Variant:
		return proof_of.call(challenge)


	func admit(request: NetwJoinRequest) -> Error:
		var error := await DiscordIdToken.admit(request, APPLICATION_ID)
		outcomes.append([error, request.account, request.account_name])
		return error


var fixture: Dictionary
var local_key: CryptoKey
var local_jwk: Dictionary
var proof_key := CryptoKey.new()
var host: Gate
var port: int
var downloads := 0


func before() -> void:
	fixture = JSON.parse_string(FileAccess.get_file_as_string(TOKENS))
	assert_int(proof_key.load_from_string(fixture.proof_key)).is_equal(OK)
	local_key = Crypto.new().generate_rsa(2048)
	var pem := local_key.save_to_string(true)
	var der := Marshalls.base64_to_raw(pem.get_slice("-----", 2).strip_edges())
	local_jwk = {
		"kid": "local-key",
		"kty": "RSA",
		"n": encoded(der.slice(33, 33 + 256)),
		"e": "AQAB",
	}


func before_test() -> void:
	host = Gate.new()
	port = (await EnetTestSupport.start_host(self, host)).port
	Netw.configure_admission(host.admit).proof(host.prove)
	assert_int(await installed(fixture.jwks)).is_equal(OK)


func after_test() -> void:
	await EnetTestSupport.stop_tree(host.get_parent())


func encoded(bytes: PackedByteArray) -> String:
	var text := Marshalls.raw_to_base64(bytes)
	return text.replace("+", "-").replace("/", "_").rstrip("=")


func signed(changes: Dictionary, kid: String = "local-key") -> String:
	var now := int(Time.get_unix_time_from_system())
	var claims := {
		"iss": "https://discord.com",
		"aud": str(APPLICATION_ID),
		"nonce": proof_key.save_to_string(true).sha256_text(),
		"sub": SUBJECT,
		"iat": now,
		"exp": now + 600,
	}
	var header := JSON.stringify({ "alg": "RS256", "kid": kid })
	var payload := JSON.stringify(claims.merged(changes, true))
	var message := encoded(header.to_utf8_buffer()) + "."
	message += encoded(payload.to_utf8_buffer())
	var signature := Crypto.new().sign(
		HashingContext.HASH_SHA256,
		message.sha256_buffer(),
		local_key,
	)
	return message + "." + encoded(signature)


func with_local_key() -> Dictionary:
	var jwks: Dictionary = fixture.jwks.duplicate(true)
	jwks.keys.append(local_jwk)
	return jwks


func answer_downloads(reply: Array) -> void:
	for request: HTTPRequest in get_tree().root.get_children().filter(
		func(child: Node) -> bool:
			return child is HTTPRequest and not child.is_queued_for_deletion(),
	):
		downloads += 1
		request.cancel_request()
		request.request_completed.emit(
			reply[0],
			reply[1],
			PackedStringArray(),
			JSON.stringify(reply[2]).to_utf8_buffer(),
		)


func installed(
		jwks: Variant,
		result := HTTPRequest.RESULT_SUCCESS,
		status := 200,
) -> Variant:
	var fetching := DiscordIdToken.fetch_keys()
	assert_object(DiscordIdToken.fetch_keys()).is_same(fetching)
	answer_downloads([result, status, jwks])
	return await fetching.wait()


func proved(token: String, signed_text: String = "") -> Callable:
	return func(challenge: String) -> PackedByteArray:
		var text := challenge if signed_text.is_empty() else signed_text
		var signature := Crypto.new().sign(
			HashingContext.HASH_SHA256,
			text.sha256_buffer(),
			proof_key,
		)
		return var_to_bytes([token, proof_key.save_to_string(true), signature])


func admitted(proof_of: Callable, keys: Variant = null) -> Array:
	var count := host.outcomes.size()
	var client := MultiplayerViewport.new()
	var gate := Gate.new()
	gate.proof_of = proof_of
	client.add_child(gate)
	add_child(client)
	Netw.configure_admission(gate.admit).proof(gate.prove)
	Netw.prepare_join(gate, &"guest")
	var peer := ENetMultiplayerPeer.new()
	assert_int(peer.create_client("127.0.0.1", port)).is_equal(OK)
	client.api.multiplayer_peer = peer

	var reply := [HTTPRequest.RESULT_SUCCESS, 200, keys]
	if keys == null:
		reply = [HTTPRequest.RESULT_CANT_CONNECT, 0, null]
	var deadline := Time.get_ticks_msec() + EnetTestSupport.WAIT_TIMEOUT_MS
	while host.outcomes.size() == count and Time.get_ticks_msec() < deadline:
		answer_downloads(reply)
		await get_tree().process_frame
	for frame in 10:
		await get_tree().process_frame
	client.queue_free()
	return host.outcomes[-1] if host.outcomes.size() > count else []


func test_a_token_admits_only_when_every_claim_holds() -> void:
	assert_int(await installed(with_local_key())).is_equal(OK)
	var now := int(Time.get_unix_time_from_system())
	var skew := DiscordIdToken.CLOCK_SKEW_SECONDS
	var valid: String = fixture.tokens.valid
	var rows := {
		"valid": [proved(valid), ACCEPTED],
		"name": [
			proved(fixture.tokens.name),
			[OK, "discord:" + SUBJECT, "Fixture Player"],
		],
		"issued inside skew": [
			proved(signed({ "iat": now + skew / 2 })),
			ACCEPTED,
		],
		"not before inside skew": [
			proved(signed({ "nbf": now + skew / 2 })),
			ACCEPTED,
		],
		"expired inside skew": [
			proved(signed({ "iat": now - 600, "exp": now - skew / 2 })),
			ACCEPTED,
		],
		"issued past skew": [
			proved(signed({ "iat": now + skew * 2 })),
			REFUSED,
		],
		"not before past skew": [
			proved(signed({ "nbf": now + skew * 2 })),
			REFUSED,
		],
		"expired past skew": [
			proved(signed({ "iat": now - 600, "exp": now - skew * 2 })),
			REFUSED,
		],
		"empty": [proved(""), REFUSED],
		"two parts": [proved("a.b"), REFUSED],
		"four parts": [proved("a.b.c.d"), REFUSED],
		"another challenge": [proved(valid, "another-challenge"), REFUSED],
		"proof not bytes": [
			func(challenge: String) -> Variant:
				return proved(valid).call(challenge).decode_var(0),
			REFUSED,
		],
	}
	for label: String in fixture.tokens:
		if label not in ["valid", "name", "unknown_key"]:
			rows[label] = [proved(fixture.tokens[label]), REFUSED]
	for label: String in rows:
		assert_array(await admitted(rows[label][0])) \
				.override_failure_message(label).is_equal(rows[label][1])


func test_an_unknown_key_downloads_once_per_cooldown() -> void:
	var rotated := with_local_key()
	var before := downloads
	assert_array(await admitted(proved(signed({ })), rotated)) \
			.is_equal(ACCEPTED)
	assert_int(downloads - before).is_equal(1)
	var unknown := proved(fixture.tokens.unknown_key)
	assert_array(await admitted(unknown, rotated)).is_equal(REFUSED)
	assert_int(downloads - before).is_equal(2)
	assert_array(await admitted(unknown, rotated)).is_equal(REFUSED)
	assert_int(downloads - before).is_equal(2)


func test_a_refused_key_download_keeps_the_trusted_keys() -> void:
	var rotated := with_local_key()
	assert_int(await installed(rotated, HTTPRequest.RESULT_CANT_CONNECT)) \
			.is_not_equal(OK)
	assert_int(await installed(rotated, HTTPRequest.RESULT_SUCCESS, 500)) \
			.is_not_equal(OK)
	assert_int(await installed({ "keys": [] })).is_not_equal(OK)
	assert_array(await admitted(proved(fixture.tokens.valid))) \
			.is_equal(ACCEPTED)
