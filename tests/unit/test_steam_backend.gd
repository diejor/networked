extends GdUnitTestSuite

func test_instantiation() -> void:
	var backend := SteamBackend.new()
	assert_object(backend).is_not_null()
	assert_bool(backend.supports_embedded_server()).is_false()

func test_setup_no_steam() -> void:
	var backend := SteamBackend.new()
	var tree := MultiplayerTree.new()
	add_child(tree)
	
	# Should fail because Steam singleton is not present in headless tests
	var err := backend.setup(tree)
	assert_int(err).is_not_equal(OK)
	
	tree.queue_free()

func test_copy_from() -> void:
	var backend := SteamBackend.new()
	var duplicate := backend.duplicate()
	assert_object(duplicate).is_not_null()
	assert_int(duplicate.max_clients).is_equal(backend.max_clients)
