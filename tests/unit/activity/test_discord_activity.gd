## Pure-logic tests for the Discord Activity layer, no Discord and no Nakama.
##
## Everything here runs headless with no server: the injected instance seam on
## [DiscordActivityService], the backend-side device-id normalization and proxy
## URL building on [NakamaDiscordRendezvous], and the dedicated WSS rendezvous.
## The live host-or-join path is exercised separately by
## [code]tests/live/nakama/test_discord_rendezvous.gd[/code].
class_name TestDiscordActivity
extends NetwTestSuite

func test_injected_instance_marks_in_discord() -> void:
	# The test fixture injects what the browser would supply, so the service
	# reports embedded and registers, with no SDK and no browser.
	var service := NetwTestDiscordService.new()
	auto_free(service)
	service.fake_instance_id = "room1"
	service.fake_device_id = "alice"

	assert_bool(service.in_discord()).is_true()
	assert_bool(service.should_register()).is_true()
	assert_str(service.instance_id()).is_equal("room1")
	assert_str(service.device_id()).is_equal("alice")


func test_not_in_discord_without_instance() -> void:
	var service := DiscordActivityService.new()
	auto_free(service)

	# No browser query string and no injected id: the service stays dormant so a
	# normal desktop or web build keeps its usual backends.
	assert_bool(service.in_discord()).is_false()
	assert_bool(service.should_register()).is_false()
	assert_str(service.instance_id()).is_equal("")


func test_normalized_device_id_clamps_to_nakama_bounds() -> void:
	var rdv := NakamaDiscordRendezvous.new()

	# Empty stays empty so the session falls back to OS.get_unique_id().
	rdv.device_id = ""
	assert_str(rdv._normalized_device_id()).is_equal("")

	# A short fake id is prefixed so it clears Nakama's 10-byte floor and two
	# instances stay distinct users.
	rdv.device_id = "alice"
	assert_str(rdv._normalized_device_id()).is_equal("netw-discord-alice")

	# A full-length Discord snowflake passes through untouched.
	rdv.device_id = "123456789012345678"
	assert_str(rdv._normalized_device_id()).is_equal("123456789012345678")

	# Anything over the 128-byte ceiling is clamped.
	rdv.device_id = "x".repeat(200)
	assert_int(rdv._normalized_device_id().length()).is_equal(128)


func test_proxy_base_uses_config_host_and_prefix() -> void:
	var rdv := NakamaDiscordRendezvous.new()

	# A host already on the iframe origin resolves without any service lookup,
	# which is how the standalone smoke scene wires the seam.
	assert_str(rdv._resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/nakama")

	# The trailing prefix is the only configurable part of the proxy base.
	rdv.proxy_prefix = "relay"
	assert_str(rdv._resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/relay")


func test_proxy_base_empty_for_direct_connection() -> void:
	var rdv := NakamaDiscordRendezvous.new()

	# A direct host with no reachable service leaves the connection untouched.
	assert_str(rdv._resolve_proxy_base(null, "127.0.0.1")).is_equal("")


func test_dedicated_rendezvous_builds_instance_keyed_url() -> void:
	var rdv := DedicatedDiscordRendezvous.new()
	rdv.public_host = "game.example.com"

	# The dedicated server keys rooms by ?instance= and elects the host itself, so
	# every participant joins the same instance-tagged WSS address.
	var target := rdv._target_for("room1")
	assert_object(target).is_not_null()
	assert_str(target.address).is_equal("wss://game.example.com/?instance=room1")
	assert_object(target.backend).is_instanceof(WebSocketBackend)


func test_dedicated_rendezvous_refuses_without_host() -> void:
	var rdv := DedicatedDiscordRendezvous.new()

	# No server configured, so the seam refuses rather than inventing a target.
	# The refusal returns before any await, so this resolves synchronously.
	var err: Error = await rdv.connect_session("room1", null, null)
	assert_int(err).is_equal(ERR_UNCONFIGURED)


func test_service_entered_does_not_install_default_auth() -> void:
	var tree := MultiplayerTree.new()
	var service := DiscordActivityService.new()
	auto_free(tree)
	auto_free(service)

	service.service_entered(tree)

	assert_object(tree.auth_provider).is_null()
	assert_object(service.rendezvous).is_null()


func test_connect_activity_refuses_without_rendezvous() -> void:
	var tree := MultiplayerTree.new()
	var service := NetwTestDiscordService.new()
	auto_free(tree)
	service.fake_instance_id = "room1"
	tree.add_child(service)

	var err: Error = await service.connect_activity(JoinPayload.new())

	assert_int(err).is_equal(ERR_UNCONFIGURED)


func test_oauth_fields_resolve_default_proxy_path() -> void:
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


func test_service_entered_binds_existing_nakama_auth() -> void:
	var tree := MultiplayerTree.new()
	var service := DiscordActivityService.new()
	var auth := NakamaAuth.new()
	auto_free(tree)
	auto_free(service)
	service.rendezvous = DedicatedDiscordRendezvous.new()
	tree.auth_provider = auth

	service.service_entered(tree)
	# The NakamaAuth bind is deferred past tree setup so get_nakama_session()
	# can add_child safely, so flush the deferred call before asserting.
	await get_tree().process_frame

	assert_object(tree.auth_provider).is_same(auth)
	assert_object(auth._session).is_same(tree.get_nakama_session())
	assert_object(auth._tree).is_same(tree)


func test_nakama_session_accepts_custom_auth_config() -> void:
	var session := NakamaSessionService.new()
	auto_free(session)

	session.configure(
		{
			"auth_mode": "custom",
			"custom_id": "123456789012345678",
			"auth_vars": { "discord_token": "token" },
		},
	)

	assert_str(session.auth_mode).is_equal("custom")
	assert_str(session.custom_id).is_equal("123456789012345678")
	assert_str(String(session.auth_vars.get("discord_token", ""))) \
			.is_equal("token")


func test_nakama_auth_authenticates_presence_identity() -> void:
	var auth := _bound_nakama_auth("nk-user-1", "Diego")
	var session := _FakeNakamaSession.new()
	session._uid = "nk-user-1"
	session._uname = "Diego"
	auth.bind_session(session)

	var prep_err := await auth.prepare(JoinPayload.new())
	assert_int(prep_err).is_equal(OK)

	var creds := auth.get_credentials(JoinPayload.new())
	var identity := auth.authenticate(2, creds)

	assert_object(identity).is_not_null()
	assert_str(identity.external_id).is_equal("nk-user-1")
	assert_str(String(identity.username)).is_equal("Diego")
	assert_str(String(identity.service)).is_equal("nakama")
	assert_bool(identity.metadata.get("verified", false)).is_true()


func test_nakama_auth_rejects_empty_presence_identity() -> void:
	var auth := _bound_nakama_auth("", "")
	var session := _FakeNakamaSession.new()
	auth.bind_session(session)

	var creds := auth.get_credentials(JoinPayload.new())
	var identity := auth.authenticate(2, creds)

	assert_object(identity).is_null()
	assert_str(auth.rejection_reason).is_equal(
		"Peer Nakama identity not found in presence",
	)


func test_nakama_auth_authenticates_local_host_identity() -> void:
	var auth := NakamaAuth.new()
	var session := _FakeNakamaSession.new()
	session._uid = "nk-host-1"
	session._uname = "HostAlice"
	auth.bind_session(session)

	var identity := auth.get_host_identity()

	assert_object(identity).is_not_null()
	assert_str(identity.external_id).is_equal("nk-host-1")
	assert_str(String(identity.username)).is_equal("HostAlice")
	assert_bool(identity.metadata.get("verified", false)).is_true()


# Builds a NakamaAuth wired to a tree whose relay presence attests
# attested_uid and attested_username for every peer.
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
