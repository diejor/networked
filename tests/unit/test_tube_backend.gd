## Unit tests for [TubeBackend] local capabilities.
extends GdUnitTestSuite

func test_local_capabilities() -> void:
	var backend := TubeBackend.new()
	assert_bool(backend.supports_embedded_server()).is_true()
	# Tube uses session IDs, not localhost - query is unsupported.
	var result: ServerInfoResult = await backend.query_server_info("")
	assert_int(result.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)
	assert_bool(backend.get_address_hint().supports_probe).is_false()
