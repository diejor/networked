class_name TestDiscordActivity
extends NetwTestSuite

const _ERROR_SCRUBBER := preload(
	"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)


func test_discord_instance_state_flow() -> void:
	var injected := NetwTestDiscordService.new()
	auto_free(injected)
	injected.fake_instance_id = "room1"
	injected.fake_device_id = "alice"
	assert_bool(injected.in_discord()).is_true()
	assert_bool(injected._should_register()).is_true()
	assert_str(injected.instance_id()).is_equal("room1")
	assert_str(injected.device_id()).is_equal("alice")

	var dormant := DiscordActivityService.new()
	auto_free(dormant)
	assert_bool(dormant.in_discord()).is_false()
	assert_bool(dormant._should_register()).is_false()
	assert_str(dormant.instance_id()).is_equal("")

	var tree := MultiplayerTree.new()
	var service := DiscordActivityService.new()
	auto_free(tree)
	auto_free(service)
	service._service_entered(tree.api)
	assert_object(service.rendezvous).is_null()

	tree = MultiplayerTree.new()
	service = NetwTestDiscordService.new()
	auto_free(tree)
	service.fake_instance_id = "room1"
	tree.add_child(service)
	var err: Error = await service.connect_activity(&"valeria")
	assert_int(err).is_equal(ERR_UNCONFIGURED)


func test_nakama_discord_rendezvous_config_flow() -> void:
	var rdv := NakamaDiscordRendezvous.new()
	rdv.device_id = ""
	assert_str(rdv._normalized_device_id()).is_equal("")

	rdv.device_id = "alice"
	assert_str(rdv._normalized_device_id()).is_equal("netw-discord-alice")

	rdv.device_id = "123456789012345678"
	assert_str(rdv._normalized_device_id()).is_equal("123456789012345678")

	rdv.device_id = "x".repeat(200)
	assert_int(rdv._normalized_device_id().length()).is_equal(128)

	assert_str(rdv._resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/nakama")

	rdv.proxy_prefix = "relay"
	assert_str(rdv._resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/relay")
	assert_str(rdv._resolve_proxy_base(null, "127.0.0.1")).is_equal("")

	var service := DiscordActivityService.new()
	auto_free(service)
	assert_str(service.token_endpoint).is_equal("token")
	assert_array(service.scopes).contains_exactly(
		["identify", "rpc.activities.write"],
	)
	assert_str(service._absolute_token_url()).is_equal("/.proxy/token")

	service.token_endpoint = "https://example.com/token"
	assert_str(service._absolute_token_url()).is_equal("https://example.com/token")

	service.token_endpoint = ""
	assert_str(service._absolute_token_url()).is_equal("")


func test_dedicated_rendezvous_flow() -> void:
	var rdv := DedicatedDiscordRendezvous.new()
	rdv.public_host = "game.example.com"

	assert_str(rdv._address_for("room1")).is_equal(
		"wss://game.example.com/?instance=room1",
	)

	rdv.public_host = ""
	var err: Error = await rdv.connect_session("room1", null, &"valeria", [])
	assert_int(err).is_equal(ERR_UNCONFIGURED)


func test_nakama_auth_admits_only_the_attested_username() -> void:
	var auth := _bound_nakama_auth("nk-user-1", "Diego")
	assert_int(auth.admit(2, &"Diego", [])).is_equal(OK)
	assert_int(auth.admit(2, &"Mallory", [])).is_equal(ERR_UNAUTHORIZED)

	auth = _bound_nakama_auth("", "")
	assert_int(auth.admit(2, &"Diego", [])).is_equal(ERR_UNAUTHORIZED)

	var unbound := NakamaAuth.new()
	auto_free(unbound)
	assert_int(unbound.admit(2, &"Diego", [])).is_equal(ERR_UNAVAILABLE)
	await _ERROR_SCRUBBER.erase_matching(["NakamaAuth: the relay presence"])


func test_nakama_session_configure_flow() -> void:
	var nakama_session := NakamaSessionService.new()
	auto_free(nakama_session)
	nakama_session.configure(
		{
			"auth_mode": "custom",
			"custom_id": "123456789012345678",
			"auth_vars": { "discord_token": "token" },
		},
	)
	assert_str(nakama_session.auth_mode).is_equal("custom")
	assert_str(nakama_session.custom_id).is_equal("123456789012345678")
	assert_str(String(nakama_session.auth_vars.get("discord_token", ""))) \
			.is_equal("token")


func _bound_nakama_auth(
		attested_uid: String,
		attested_username: String,
) -> NakamaAuth:
	var tree := MultiplayerTree.new()
	var dir := NakamaLobbyDirectory.new()
	var wrapper := _FakeNakamaWrapper.new()
	auto_free(tree)
	auto_free(dir)
	add_child(tree)
	tree.add_child(dir)
	Netw.service_register(tree, dir)
	dir._wrapper = wrapper
	wrapper.attested_user_id = attested_uid
	wrapper.attested_username = attested_username
	var auth := NakamaAuth.new()
	auto_free(auth)
	auth.bind_tree(tree)
	return auth


class _FakeNakamaWrapper:
	extends NakamaWrapper

	var attested_user_id := ""
	var attested_username := ""


	func user_id_for_peer(_peer_id: int) -> String:
		return attested_user_id


	func username_for_peer(_peer_id: int) -> String:
		return attested_username
