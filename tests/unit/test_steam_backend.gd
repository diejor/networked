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

	# In CI runners, the Steam singleton may be present (GDExtension loaded)
	# but initialization will fail because the Steam client isn't running.
	if service.is_ready():
		assert_int(err).is_equal(OK)
	else:
		assert_int(err).is_equal(ERR_CANT_CREATE)

	tree.queue_free()

func test_copy_from() -> void:
	var backend := SteamBackend.new()
	var dup := backend.duplicate()
	assert_object(dup).is_not_null()
	assert_int(dup.max_clients).is_equal(backend.max_clients)
