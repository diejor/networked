## Unit tests for [method BackendPeer.is_available] across the built-in
## transports. Availability is the platform gate, separate from probing.
class_name TestBackendAvailability
extends NetwTestSuite

# Self-contained transports that ship a web export stay available everywhere.
func test_websocket_is_available_everywhere() -> void:
	assert_bool(WebSocketBackend.new().is_available()).is_true()


func test_local_loopback_is_available_everywhere() -> void:
	assert_bool(LocalLoopbackBackend.new().is_available()).is_true()


# ENet has no web export, so availability follows the platform web feature.
func test_enet_excludes_web() -> void:
	assert_bool(ENetBackend.new().is_available()).is_equal(
		not OS.has_feature("web"),
	)
