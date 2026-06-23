## Unit tests for [SteamBackend] metadata and config behavior.
class_name TestSteamBackend
extends NetwTestSuite

func test_supports_embedded_server_is_false() -> void:
	var backend := SteamBackend.new()
	assert_bool(backend.supports_embedded_server()).is_false()


# Steam has no web export, so availability follows the platform web feature.
func test_is_available_excludes_web() -> void:
	var backend := SteamBackend.new()
	assert_bool(backend.is_available()).is_equal(not OS.has_feature("web"))




# Steam lobby status comes from the directory's lobby list, not a probe,
# so probe_server_info is always unsupported.
func test_query_is_unsupported() -> void:
	var backend := SteamBackend.new()

	var valid: BackendPeer.ProbeResult = backend.probe_server_info("123")
	var invalid: BackendPeer.ProbeResult = backend.probe_server_info("not-int")

	assert_int(valid.status).is_equal(BackendPeer.ProbeResult.Status.UNSUPPORTED)
	assert_int(invalid.status).is_equal(BackendPeer.ProbeResult.Status.UNSUPPORTED)
