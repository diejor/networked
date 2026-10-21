class_name TestDiscordActivity
extends NetwTestSuite

const _ERROR_SCRUBBER := preload(
	"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)
const HOST_PORT := 30199


class ScriptedRendezvous:
	extends DiscordRendezvous

	var found: NetwEndpoint
	var winner: NetwEndpoint
	var transport: StringName = &""
	var claims := 0


	func find(_instance_id: String, _connection: NetwConnection) -> NetwEndpoint:
		return found


	func host_transport() -> StringName:
		return transport


	func host_settings() -> Dictionary:
		return { port = HOST_PORT }


	func claim(_instance_id: String, _connection: NetwConnection) -> NetwEndpoint:
		claims += 1
		return winner


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


func test_connect_activity_needs_a_rendezvous() -> void:
	var service := build_service(null)
	var err: Error = await service.connect_activity(&"valeria")
	assert_int(err).is_equal(ERR_UNCONFIGURED)
	assert_int(
		await _ERROR_SCRUBBER.erase_matching(
			["Set DiscordActivityService.rendezvous to"],
		),
	).is_equal(1)


func test_a_rendezvous_that_finds_nothing_and_never_hosts_fails() -> void:
	var service := build_service(ScriptedRendezvous.new())
	var failures: Array[String] = []
	service.activity_failed.connect(failures.append)
	var err: Error = await service.connect_activity(&"valeria")
	assert_int(err).is_equal(ERR_UNAVAILABLE)
	assert_array(failures).has_size(1)


func test_connect_activity_joins_what_the_rendezvous_finds() -> void:
	var host := await EnetTestSupport.start_host(self)
	var rendezvous := ScriptedRendezvous.new()
	var service := build_service(rendezvous)
	var session: NetwSession = Netw.session(service)
	rendezvous.found = session.connection.add(
		&"ENetMultiplayerPeer",
		"127.0.0.1:%d" % host.port,
	)

	var err: Error = await service.connect_activity(&"valeria")

	assert_int(err).is_equal(OK)
	assert_int(session.state).is_equal(NetwSession.STATE_ONLINE)
	assert_int(rendezvous.claims).is_equal(0)
	await EnetTestSupport.stop_tree(host.tree)


func test_connect_activity_hosts_when_nothing_is_found() -> void:
	var rendezvous := ScriptedRendezvous.new()
	rendezvous.transport = &"ENetMultiplayerPeer"
	var service := build_service(rendezvous)
	var session: NetwSession = Netw.session(service)

	var err: Error = await service.connect_activity(&"valeria")

	assert_int(err).is_equal(OK)
	assert_int(session.state).is_equal(NetwSession.STATE_ONLINE)
	assert_int(session.role).is_not_equal(NetwSession.ROLE_CLIENT)
	assert_int(rendezvous.claims).is_equal(1)


func test_connect_activity_joins_the_winner_of_a_claim() -> void:
	var host := await EnetTestSupport.start_host(self)
	var rendezvous := ScriptedRendezvous.new()
	rendezvous.transport = &"ENetMultiplayerPeer"
	var service := build_service(rendezvous)
	var session: NetwSession = Netw.session(service)
	rendezvous.winner = session.connection.add(
		&"ENetMultiplayerPeer",
		"127.0.0.1:%d" % host.port,
	)

	var err: Error = await service.connect_activity(&"valeria")

	assert_int(err).is_equal(OK)
	assert_int(session.state).is_equal(NetwSession.STATE_ONLINE)
	assert_int(session.role).is_equal(NetwSession.ROLE_CLIENT)
	await EnetTestSupport.stop_tree(host.tree)


func test_nakama_discord_rendezvous_config_flow() -> void:
	var rdv := NakamaDiscordRendezvous.new()
	rdv.device_id = ""
	assert_str(rdv.nakama_device_id()).is_equal("")

	rdv.device_id = "alice"
	assert_str(rdv.nakama_device_id()).is_equal("netw-discord-alice")

	rdv.device_id = "123456789012345678"
	assert_str(rdv.nakama_device_id()).is_equal("123456789012345678")

	rdv.device_id = "x".repeat(200)
	assert_int(rdv.nakama_device_id().length()).is_equal(128)

	assert_str(rdv.resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/nakama")

	rdv.proxy_prefix = "relay"
	assert_str(rdv.resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/relay")
	assert_str(rdv.resolve_proxy_base(null, "127.0.0.1")).is_equal("")

	assert_str(rdv.host_transport()).is_equal("NakamaRelayPeer")
	assert_int(rdv.host_settings().visibility) \
			.is_equal(NetwServerInfo.VISIBILITY_PRIVATE)

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


func test_dedicated_rendezvous_finds_its_room() -> void:
	var rdv := DedicatedDiscordRendezvous.new()
	rdv.public_host = "game.example.com"
	var service := build_service(rdv)
	var connection: NetwConnection = Netw.session(service).connection

	var endpoint := await rdv.find("room1", connection)

	assert_str(endpoint.peer_class).is_equal("WebSocketMultiplayerPeer")
	assert_str(endpoint.address).is_equal("wss://game.example.com/?instance=room1")
	assert_str(rdv.host_transport()).is_equal("")

	rdv.public_host = ""
	assert_object(await rdv.find("room1", connection)).is_null()
	assert_int(
		await _ERROR_SCRUBBER.erase_matching(
			["Set DedicatedDiscordRendezvous.public_host to the host"],
		),
	).is_equal(1)


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


func build_service(rendezvous: DiscordRendezvous) -> NetwTestDiscordService:
	var tree := MultiplayerViewport.new()
	var service := NetwTestDiscordService.new()
	service.fake_instance_id = "room1"
	service.rendezvous = rendezvous
	tree.add_child(service)
	auto_free(tree)
	add_child(tree)
	return service
