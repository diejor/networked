## Unit tests for [SteamTransport] scheme recognition and availability.
class_name TestSteamTransport
extends NetwTestSuite

func test_recognizes_steam_scheme() -> void:
	var transport := SteamTransport.new()
	assert_str(transport.scheme()).is_equal("steam")


# Steam has no web export, so availability follows the platform web feature.
func test_is_available_excludes_web() -> void:
	var transport := SteamTransport.new()
	assert_bool(transport._is_available()).is_equal(not OS.has_feature("web"))


# Join and host recognition both key off the steam scheme.
func test_recognition_follows_scheme() -> void:
	var transport := SteamTransport.new()

	var target := NetwConnectTarget.new()
	target.scheme = &"steam"
	target.address = "123"
	assert_bool(transport._can_join(target)).is_true()

	var config := NetwHostConfig.new()
	config.scheme = &"steam"
	assert_bool(transport._can_host(config)).is_true()
