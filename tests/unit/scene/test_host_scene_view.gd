## Unit coverage for concurrency-gated host presentation.
class_name TestHostSceneView
extends NetwTestSuite

func test_single_tree_does_not_add_a_host_view() -> void:
	var tree := _tree_with_mode(NetwSceneConfig.Concurrency.SINGLE)
	add_child(tree)
	# The host view is created deferred, so let the idle callback run.
	await get_tree().process_frame

	assert_object(tree.get_node_or_null("HostSceneView")).is_null()

	tree.queue_free()
	await get_tree().process_frame


func test_concurrent_tree_adds_a_host_view() -> void:
	var tree := _tree_with_mode(NetwSceneConfig.Concurrency.CONCURRENT)
	add_child(tree)
	# The host view is created deferred, so let the idle callback run.
	await get_tree().process_frame

	assert_object(tree.get_node_or_null("HostSceneView")).is_not_null()

	tree.queue_free()
	await get_tree().process_frame


func test_default_camera_adoption_makes_player_camera_current() -> void:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(320, 180)
	add_child(viewport)
	var player := Node2D.new()
	var camera := Camera2D.new()
	player.add_child(camera)
	viewport.add_child(player)
	var view: HostSceneView = auto_free(HostSceneView.new())

	view._adopt_camera(player, null)

	assert_object(viewport.get_camera_2d()).is_same(camera)

	viewport.queue_free()
	await get_tree().process_frame


func _tree_with_mode(
		mode: NetwSceneConfig.Concurrency,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.desired_role = NetwSessionInterface.Role.LISTEN_SERVER
	var manager := MultiplayerSceneManager.new()
	manager.concurrency = mode
	tree.add_child(manager)
	return tree
