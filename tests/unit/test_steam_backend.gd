## Unit tests for [SteamBackend] metadata and config behavior.
class_name TestSteamBackend
extends NetwTestSuite


class _FakeSteamDirectory:
	extends SteamLobbyDirectory

	func _enter_tree() -> void:
		pass

	func _exit_tree() -> void:
		pass


func test_supports_embedded_server_is_false() -> void:
	var backend := SteamBackend.new()
	assert_bool(backend.supports_embedded_server()).is_false()


func test_copy_from_preserves_server_name() -> void:
	var source := SteamBackend.new()
	source.server_name = "Lobby A"
	var target := SteamBackend.new()

	target.copy_from(source)

	assert_that(target.server_name).is_equal("Lobby A")


func test_query_invalid_lobby_id_returns_error() -> void:
	var backend := SteamBackend.new()

	var result: ServerInfoResult = await backend.query_server_info("not-int")

	assert_int(result.status).is_equal(ServerInfoResult.Status.ERROR)
	assert_that(result.message).is_equal("Invalid Steam lobby ID.")


func test_query_without_directory_is_unsupported() -> void:
	var backend := SteamBackend.new()

	var result: ServerInfoResult = await backend.query_server_info("123")

	assert_int(result.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)


func test_query_uses_directory_after_setup() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)
	var directory := _FakeSteamDirectory.new()
	var wrapper := NetwMockSteamWrapper.new()
	wrapper.lobby_name = "Fake Lobby"
	directory._wrapper = wrapper
	tree.add_child(directory)
	tree.register_service(directory, SteamLobbyDirectory)

	var backend := SteamBackend.new()
	var setup_err: Error = await backend.setup(tree)
	var result: ServerInfoResult = await backend.query_server_info("123", 0.1)

	assert_int(setup_err).is_equal(OK)
	assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
	assert_that(result.info.motd).is_equal("Fake Lobby")
	assert_int(result.info.players).is_equal(2)
	assert_int(result.info.max_players).is_equal(8)
	assert_int(wrapper.requested_lobby_id).is_equal(123)
	tree.queue_free()


func test_probe_session_sets_up_steam_backend_before_query() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)
	var directory := _FakeSteamDirectory.new()
	var wrapper := NetwMockSteamWrapper.new()
	directory._wrapper = wrapper
	tree.add_child(directory)
	tree.register_service(directory, SteamLobbyDirectory)

	var target := JoinTarget.new()
	target.backend = SteamBackend.new()
	target.address = "456"
	var session := ProbeSession.new(target, 0.1, tree)

	var result: ServerInfoResult = await session.run()

	assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
	assert_int(wrapper.requested_lobby_id).is_equal(456)
	tree.queue_free()
