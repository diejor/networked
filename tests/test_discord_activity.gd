class_name TestDiscordActivity
extends NetwTestSuite

const APP := 42
const INSTANCE := "i-1520000000000000000-gc-1-2"
const PLAYER := "205519959982473217"
const WORKER_KEY := "res://tests/discord_activity/test_only_worker_key.pem"
const WORKER_PUB := "res://tests/discord_activity/test_only_worker_key.pub"
const WAIT_SECONDS := 15.0


class Gate extends Node:
	var requests: Array[NetwJoinRequest] = []


	func admit(request: NetwJoinRequest) -> Error:
		if request.peer_id == 1:
			request.account = ""
			return OK
		requests.append(request)
		return OK


class SlowWorker extends NetwTestDiscordService:
	func _grant(nonce: String) -> NetwPromise:
		var granting := NetwPromise.new()
		get_tree().create_timer(3.5).timeout.connect(
			func() -> void: granting.resolve({ "id_token": fake_worker.call(nonce) })
		)
		return granting


var trees: Array[MultiplayerViewport] = []


func after_test() -> void:
	for tree in trees:
		await EnetTestSupport.stop_tree(tree)
	trees.clear()


func test_entering_an_activity_invites_its_room() -> void:
	var tree := session_tree()
	var invites := watch_invites(tree)
	var discord := NetwTestDiscordService.new()
	discord.application_id = APP
	discord.fake_instance_id = INSTANCE
	discord.room_transport = &"ENetMultiplayerPeer"
	tree.add_child(discord)
	await NetwTestSuite.drain_frames(get_tree(), 2)

	assert_object(Netw.session(tree).service(NetwTestDiscordService)) \
			.is_same(discord)
	assert_array(invites).is_equal(["room:discord:42:%s" % INSTANCE])
	var room := Netw.session(tree).connection.endpoints[0]
	assert_bool(room.is_invited).is_true()


func test_nothing_registers_outside_an_activity() -> void:
	var tree := session_tree()
	var invites := watch_invites(tree)
	var discord := DiscordActivityService.new()
	discord.room_transport = &"ENetMultiplayerPeer"
	tree.add_child(discord)
	await NetwTestSuite.drain_frames(get_tree(), 2)

	assert_str(discord.instance_id).is_empty()
	assert_object(Netw.session(tree).service(DiscordActivityService)).is_null()
	assert_array(invites).is_empty()
	var signing_in := discord.sign_in()
	assert_int(signing_in.code).is_equal(ERR_UNAVAILABLE)


func test_a_player_in_the_activity_joins_as_their_discord_account(
		timeout := 30000,
) -> void:
	var discord := NetwTestDiscordService.new()
	discord.fake_instance_id = INSTANCE

	var request := await join_with(discord)

	var account := "discord:" + PLAYER
	assert_array(request.accounts).contains_exactly([account])
	assert_str(request.name_of(account)).is_equal("Ana")


func test_a_player_who_takes_long_to_authorize_still_joins(
		timeout := 30000,
) -> void:
	var host_gate := Gate.new()
	var host := await EnetTestSupport.start_host(self, host_gate)
	var client := EnetTestSupport.make_client_tree(self, host.port)
	trees.append_array([host.tree, client])
	var client_gate := Gate.new()
	client.add_child(client_gate)
	for tree: MultiplayerViewport in [host.tree, client]:
		var discord := SlowWorker.new()
		discord.application_id = APP
		discord.fake_instance_id = INSTANCE
		discord.room_transport = &""
		discord.fake_worker = worker_token
		tree.add_child(discord)
	Netw.configure_admission(host_gate.admit) \
			.accept(DiscordIdToken.signed_by(APP, WORKER_PUB))
	Netw.configure_admission(client_gate.admit) \
			.accept(DiscordIdToken.signed_by(APP, WORKER_PUB))

	assert_int(await EnetTestSupport.join_client(client, host.port, &"ana")) \
			.is_equal(OK)
	assert_array(host_gate.requests.back().accounts) \
			.contains_exactly(["discord:" + PLAYER])


func test_outside_an_activity_no_discord_account_is_offered(
		timeout := 30000,
) -> void:
	var request := await join_with(NetwTestDiscordService.new())

	assert_array(request.accounts).is_empty()


func test_url_mappings_reach_a_host_through_the_proxy() -> void:
	var discord: DiscordActivityService = auto_free(DiscordActivityService.new())
	discord.application_id = APP
	discord.url_mappings = {
		"/nakama": "nakama.example.com",
		"/api/": "example.com/v1/",
	}
	var proxy := "42.discordsays.com/.proxy"
	for row: Array in [
		["https://nakama.example.com", "https://%s/nakama" % proxy],
		[
			"https://nakama.example.com:443/v2/account?unwrap",
			"https://%s/nakama/v2/account?unwrap" % proxy,
		],
		["wss://nakama.example.com/ws", "wss://%s/nakama/ws" % proxy],
		["https://example.com/v1/users", "https://%s/api/users" % proxy],
		["/token", "https://%s/token" % proxy],
		["https://nakama.example.com.net/", "https://nakama.example.com.net/"],
		["https://example.com/v2", "https://example.com/v2"],
		["http://nakama.example.com/", "http://nakama.example.com/"],
	]:
		assert_str(discord.proxied_url(row[0])).is_equal(row[1])


func session_tree() -> MultiplayerViewport:
	var tree: MultiplayerViewport = auto_free(MultiplayerViewport.new())
	add_child(tree)
	return tree


func watch_invites(tree: MultiplayerViewport) -> Array[String]:
	var invites: Array[String] = []
	Netw.session(tree).connection.invited.connect(
		func(endpoint: NetwEndpoint) -> void: invites.append(endpoint.address),
	)
	return invites


func join_with(discord: NetwTestDiscordService) -> NetwJoinRequest:
	var host_gate := Gate.new()
	var host := await EnetTestSupport.start_host(self, host_gate, WAIT_SECONDS)
	var client := EnetTestSupport.make_client_tree(self, host.port)
	trees.append_array([host.tree, client])
	client.api.inner.auth_timeout = WAIT_SECONDS
	var client_gate := Gate.new()
	client.add_child(client_gate)
	discord.application_id = APP
	discord.room_transport = &""
	discord.fake_worker = worker_token
	client.add_child(discord)
	Netw.configure_admission(host_gate.admit) \
			.accept(DiscordIdToken.signed_by(APP, WORKER_PUB))
	Netw.configure_admission(client_gate.admit) \
			.accept(DiscordIdToken.signed_by(APP, WORKER_PUB))

	assert_int(await EnetTestSupport.join_client(client, host.port, &"ana")) \
			.is_equal(OK)
	assert_int(host_gate.requests.size()).is_equal(1)
	return host_gate.requests.back()


func worker_token(nonce: String) -> String:
	var key := CryptoKey.new()
	key.load(WORKER_KEY)
	var now := int(Time.get_unix_time_from_system())
	var header := { "alg": "RS256", "typ": "JWT" }
	var claims := {
		"iss": "https://%d.discordsays.com" % APP,
		"aud": str(APP),
		"sub": PLAYER,
		"name": "Ana",
		"nonce": nonce,
		"instance": INSTANCE,
		"iat": now,
		"exp": now + 3600,
	}
	var body := "%s.%s" % [
		base64url(JSON.stringify(header).to_utf8_buffer()),
		base64url(JSON.stringify(claims).to_utf8_buffer()),
	]
	var signature := Crypto.new().sign(
		HashingContext.HASH_SHA256,
		body.sha256_buffer(),
		key,
	)
	return body + "." + base64url(signature)


func base64url(bytes: PackedByteArray) -> String:
	return Marshalls.raw_to_base64(bytes) \
			.replace("+", "-").replace("/", "_").rstrip("=")
