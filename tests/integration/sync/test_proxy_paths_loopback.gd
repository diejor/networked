## Integration tests for [ProxySynchronizer] over a loopback session.
class_name TestProxyPathsLoopback
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	var proxy := ProbeProxy.new()
	proxy.name = "ProbeProxy"

	player_builder = PlayerBuilder.new("TestProbePlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_synchronizer(proxy, "Components")
	player_builder.pack()

	level_builder = LevelBuilder.new("TestProbeLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.add_spawnable_scene(level_builder.resource_path)
		return sm
	await harness.setup(sm_factory)
	client0 = await harness.add_client()


func test_on_change_replicates_through_proxy_virtual_path() -> void:
	var server_player := harness.spawn_player(
		client0,
		player_builder.packed,
	) as Node2D
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D

	client_player.position = Vector2(100.0, 200.0)

	var received := false
	for _i in 10:
		await get_tree().process_frame
		if server_player.position.is_equal_approx(Vector2(100.0, 200.0)):
			received = true
			break

	assert_that(received).is_true()
	assert_that(server_player.position).is_equal(Vector2(100.0, 200.0))


func test_proxy_config_uses_node_traversal_virtual_path() -> void:
	var root := Node2D.new()
	root.name = "Root"
	add_child(root)
	auto_free(root)

	var components := Node.new()
	components.name = "Components"
	root.add_child(components)

	var proxy := ProbeProxy.new()
	proxy.name = "ProbeProxy"
	proxy.root_path = NodePath("../../")
	components.add_child(proxy)

	var expected_vpath := NodePath("Components/ProbeProxy:position")
	var old_style_vpath := NodePath(":position")

	assert_that(proxy.replication_config.has_property(expected_vpath)).is_true()
	assert_that(proxy.replication_config.has_property(old_style_vpath)).is_false()


func test_root_path_points_to_entity_root_not_dot() -> void:
	var container := Node.new()
	add_child(container)
	auto_free(container)
	var player := player_builder.packed.instantiate()
	container.add_child(player)

	var proxy: ProbeProxy = player.get_node("Components/ProbeProxy")

	assert_that(proxy.root_path).is_not_equal(NodePath("."))
	var resolved := proxy.get_node_or_null(proxy.root_path)
	assert_that(resolved).is_equal(player)
