extends GdUnitTestSuite

func test_local_capabilities() -> void:
	var backend := TubeBackend.new()
	assert_bool(backend.supports_embedded_server()).is_true()
	assert_bool(backend.supports_local_probe()).is_false()
