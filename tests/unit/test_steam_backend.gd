## Unit tests for [SteamBackend] metadata and config behavior.
class_name TestSteamBackend
extends NetwTestSuite


func test_supports_embedded_server_is_false() -> void:
	var backend := SteamBackend.new()
	assert_bool(backend.supports_embedded_server()).is_false()


func test_copy_from_preserves_server_name() -> void:
	var source := SteamBackend.new()
	source.server_name = "Lobby A"
	var target := SteamBackend.new()

	target.copy_from(source)

	assert_that(target.server_name).is_equal("Lobby A")


# Steam lobby status comes from the directory's lobby list, not a probe,
# so query_server_info is always unsupported.
func test_query_is_unsupported() -> void:
	var backend := SteamBackend.new()

	var valid: ServerInfoResult = backend.query_server_info("123")
	var invalid: ServerInfoResult = backend.query_server_info("not-int")

	assert_int(valid.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)
	assert_int(invalid.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)


func test_probe_session_steam_target_is_unsupported() -> void:
	var target := JoinTarget.new()
	target.backend = SteamBackend.new()
	target.address = "456"
	var session := ProbeSession.new(target, 0.1)

	var result: ServerInfoResult = await session.run()

	assert_int(result.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)
