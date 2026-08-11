## Checks for the lobby-backed connect kit transports ([SteamTransport],
## [NakamaTransport]). The live lobby paths need their SDKs, so these cover
## recognition and the graceful no-directory-registered failure.
class_name TestConnectKitLobby
extends NetwTestSuite

func test_steam_recognizes_its_scheme() -> void:
	var t := SteamTransport.new()
	var target := NetwConnectTarget.new()
	target.scheme = &"steam"
	assert_bool(t._can_join(target)).is_true()
	var config := NetwHostConfig.new()
	config.transport = NetwSteamParams.new()
	assert_bool(t._can_host(config)).is_true()


func test_nakama_recognizes_its_scheme() -> void:
	var t := NakamaTransport.new()
	var target := NetwConnectTarget.new()
	target.scheme = &"nakama"
	assert_bool(t._can_join(target)).is_true()
	var config := NetwHostConfig.new()
	config.transport = NetwNakamaParams.new()
	assert_bool(t._can_host(config)).is_true()


func test_steam_join_without_directory_errors() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [SteamTransport.new()]

	var target := NetwConnectTarget.new()
	target.scheme = &"steam"
	target.address = "123"
	var result := await NetwConnector.of(api).join(target, null, true)

	assert_int(NetwConnector.error_of(result)).is_equal(ERR_CANT_CONNECT)
	assert_int(NetwConnector.of(api).current_attempt.result.status) \
			.is_equal(NetwConnectResult.Status.ERROR)
	api.embedding.dispose()


func test_nakama_host_without_directory_errors() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [NakamaTransport.new()]

	var config := NetwHostConfig.new()
	config.transport = NetwNakamaParams.new()
	var result := await NetwConnector.of(api).host(null, config)

	assert_int(NetwConnector.error_of(result)).is_equal(ERR_CANT_CREATE)
	assert_int(NetwConnector.of(api).current_attempt.result.status) \
			.is_equal(NetwConnectResult.Status.ERROR)
	api.embedding.dispose()
