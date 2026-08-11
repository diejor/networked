## Unit coverage for isolation-derived host presentation.
class_name TestHostSceneView
extends NetwTestSuite

func test_a_shared_world_host_adds_no_view() -> void:
	var tree := _host_tree(NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE)
	add_child(tree)
	# The host view is created deferred, so let the idle callback run.
	await get_tree().process_frame

	assert_object(tree.get_node_or_null("HostSceneView")).is_null()

	tree.queue_free()
	await get_tree().process_frame


func test_an_isolated_world_host_adds_a_view() -> void:
	var tree := _host_tree(NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD)
	add_child(tree)
	await get_tree().process_frame

	# Nothing is live yet, so there is no world to draw.
	assert_object(tree.get_node_or_null("HostSceneView")).is_null()

	var scene := tree.api.scene_spawn(
		_level().resource_path,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
	)
	assert_object(scene).is_not_null()
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


func _host_tree(
		isolation: NetwMultiplayer.SceneIsolation,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.desired_role = NetwMultiplayer.Role.LISTEN_SERVER
	var manager := MultiplayerSceneManager.new()
	manager.scene_isolation = isolation
	tree.add_child(manager)
	return tree


# A trivial packed level the registry can spawn a container around.
func _level() -> PackedScene:
	var builder := LevelBuilder.new("HostViewLevel").with_root(Node2D)
	builder.pack()
	return builder.packed
