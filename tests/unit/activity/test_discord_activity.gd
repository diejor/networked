## Pure logic tests for the Discord Activity layer.
class_name TestDiscordActivity
extends NetwTestSuite

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
	assert_object(tree.api._session.auth_flow).is_null()
	assert_object(service.rendezvous).is_null()

	tree = MultiplayerTree.new()
	service = NetwTestDiscordService.new()
	auto_free(tree)
	service.fake_instance_id = "room1"
	tree.add_child(service)
	var err: Error = await service.connect_activity(JoinPayload.new())
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

	var target := rdv._target_for("room1")
	assert_object(target).is_not_null()
	assert_str(target.address).is_equal("wss://game.example.com/?instance=room1")
	assert_str(target.scheme).is_equal("ws")

	rdv.public_host = ""
	var err: Error = await rdv.connect_session("room1", null, null)
	assert_int(err).is_equal(ERR_UNCONFIGURED)


func test_nakama_auth_identity_flow() -> void:
	var tree := MultiplayerTree.new()
	var service := DiscordActivityService.new()
	var auth := NakamaAuth.new()
	auto_free(tree)
	auto_free(service)
	# Mount the tree so its session resolves through api.root, the way a service
	# under a live tree does, since the session no longer holds a tree back-ref.
	add_child(tree)
	service.rendezvous = DedicatedDiscordRendezvous.new()
	tree.api.session.set_auth_flow(auth)
	service._service_entered(tree.api)
	await get_tree().process_frame
	assert_object(tree.api._session.auth_flow).is_same(auth)
	assert_object(auth._session).is_same(tree.get_nakama_session())
	assert_object(auth._tree).is_same(tree)

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

	auth = _bound_nakama_auth("nk-user-1", "Diego")
	var fake_session := _FakeNakamaSession.new()
	fake_session._uid = "nk-user-1"
	fake_session._uname = "Diego"
	auth.bind_session(fake_session)
	var prep_err := await auth.prepare(JoinPayload.new())
	assert_int(prep_err).is_equal(OK)
	var result := auth.verify(2, auth.credentials(JoinPayload.new()))
	_assert_identity(result.identity, "nk-user-1", "Diego")
	assert_str(String(result.identity.service)).is_equal("nakama")

	auth = _bound_nakama_auth("", "")
	fake_session = _FakeNakamaSession.new()
	auth.bind_session(fake_session)
	result = auth.verify(2, auth.credentials(JoinPayload.new()))
	assert_bool(result.accepted).is_false()
	assert_str(result.rejection_reason).is_equal(
		"Peer Nakama identity not found in presence",
	)

	auth = NakamaAuth.new()
	fake_session = _FakeNakamaSession.new()
	fake_session._uid = "nk-host-1"
	fake_session._uname = "HostAlice"
	auth.bind_session(fake_session)
	var identity := auth.host_identity()
	_assert_identity(identity, "nk-host-1", "HostAlice")


func _assert_identity(
		identity: NetwIdentity,
		expected_id: String,
		expected_username: String,
) -> void:
	assert_object(identity).is_not_null()
	assert_str(identity.external_id).is_equal(expected_id)
	assert_str(String(identity.username)).is_equal(expected_username)
	assert_bool(identity.metadata.get("verified", false)).is_true()


func _bound_nakama_auth(
		attested_uid: String,
		attested_username: String,
) -> NakamaAuth:
	var tree := MultiplayerTree.new()
	var dir := NakamaLobbyDirectory.new()
	var wrapper := _FakeNakamaWrapper.new()
	auto_free(tree)
	auto_free(dir)
	tree.add_child(dir)
	tree.register_service(dir)
	dir._wrapper = wrapper
	wrapper.attested_user_id = attested_uid
	wrapper.attested_username = attested_username
	var auth := NakamaAuth.new()
	auth.bind_tree(tree)
	return auth


class _FakeNakamaSession:
	var _uid := ""
	var _uname := ""
	var _authenticated := true


	func is_authenticated() -> bool:
		return _authenticated


	func local_user_id() -> String:
		return _uid


	func local_username() -> String:
		return _uname


class _FakeNakamaWrapper:
	extends NakamaWrapper

	var attested_user_id := ""
	var attested_username := ""


	func user_id_for_peer(_peer_id: int) -> String:
		return attested_user_id


	func username_for_peer(_peer_id: int) -> String:
		return attested_username
