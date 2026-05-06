extends GdUnitTestSuite

func test_instantiation() -> void:
	var backend := SteamBackend.new()
	assert_object(backend).is_not_null()
	assert_bool(backend.supports_embedded_server()).is_false()

func test_setup_creates_service() -> void:
	var backend := SteamBackend.new()
	var tree := MultiplayerTree.new()
	add_child(tree)

	var err := backend.setup(tree)
	var service := tree.get_service(SteamService)
	assert_object(service).is_not_null()
	# Result depends on whether Steam is available in the environment.
	if Engine.has_singleton("Steam"):
		assert_int(err).is_equal(OK)
	else:
		assert_int(err).is_not_equal(OK)

	tree.queue_free()

func test_copy_from() -> void:
	var backend := SteamBackend.new()
	var duplicate := backend.duplicate()
	assert_object(duplicate).is_not_null()
	assert_int(duplicate.max_clients).is_equal(backend.max_clients)
