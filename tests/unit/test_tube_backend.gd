## Unit tests for [TubeBackend] local capabilities.
extends GdUnitTestSuite

func test_local_capabilities() -> void:
	var backend := TubeBackend.new()
	assert_bool(backend.supports_embedded_server()).is_true()
	# Tube uses session IDs, not localhost - probe is unsupported.
	assert_bool(backend.probe("").is_unsupported()).is_true()
	assert_bool(backend.get_address_hint().supports_probe).is_false()
