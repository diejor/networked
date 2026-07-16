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
	config.scheme = &"steam"
	assert_bool(t._can_host(config)).is_true()


func test_nakama_recognizes_its_scheme() -> void:
	var t := NakamaTransport.new()
	var target := NetwConnectTarget.new()
	target.scheme = &"nakama"
	assert_bool(t._can_join(target)).is_true()
	var config := NetwHostConfig.new()
	config.scheme = &"nakama"
	assert_bool(t._can_host(config)).is_true()


func test_steam_join_without_directory_errors() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var connector := NetwConnector.new(api)
	connector.transports = [SteamTransport.new()]

	var target := NetwConnectTarget.new()
	target.scheme = &"steam"
	target.address = "123"
	var attempt := connector.join(target)
	if not attempt.is_done():
		await attempt.finished

	assert_int(attempt.result.status).is_equal(NetwConnectResult.Status.ERROR)
	api.dispose()


func test_nakama_host_without_directory_errors() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var connector := NetwConnector.new(api)
	connector.transports = [NakamaTransport.new()]

	var config := NetwHostConfig.new()
	config.scheme = &"nakama"
	var attempt := connector.host(config)
	if not attempt.is_done():
		await attempt.finished

	assert_int(attempt.result.status).is_equal(NetwConnectResult.Status.ERROR)
	api.dispose()
