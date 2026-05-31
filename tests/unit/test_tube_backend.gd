## Unit tests for [TubeBackend] local capabilities.
extends GdUnitTestSuite

func test_local_capabilities() -> void:
	var backend := TubeBackend.new()
	assert_bool(backend.supports_embedded_server()).is_true()
	# Tube uses session IDs, not localhost - query is unsupported.
	var result: ServerInfoResult = backend.query_server_info("")
	assert_int(result.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)
	assert_bool(backend.get_address_hint().supports_probe).is_false()


func test_setup_requires_tube_client_descendant() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)
	var backend := TubeBackend.new()

	var err := backend.setup(tree)

	assert_int(err).is_equal(ERR_UNCONFIGURED)
	tree.queue_free()
