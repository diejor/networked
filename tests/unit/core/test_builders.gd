## Unit tests for programmatic builder primitives.
##
## Hard-wrapped to 80 columns.
class_name TestBuilders
extends NetwTestSuite

const MINIMAL_PLAYER_TSCN := preload(
	"res://addons/networked_test/fixtures/TestPlayerMinimal.tscn"
)
const TEST_PLAYER_WITH_SAVE_TSCN := preload(
	"res://addons/networked_test/fixtures/TestPlayerWithSave.tscn"
)
const TEST_LEVEL_TSCN := preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)


func test_scene_assembly_attach() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var child: Node2D = Node2D.new()
	var grand_child: Node2D = Node2D.new()
	child.add_child(grand_child)
	var _attached: Node = SceneAssembly.attach(root, child, root)
	assert_that(child.get_parent()).is_equal(root)
	assert_that(child.owner).is_equal(root)
	assert_that(grand_child.owner).is_equal(root)


func test_scene_assembly_pack_with_path() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Root"
	var child: Node2D = Node2D.new()
	child.name = "Child"
	var _attached: Node = SceneAssembly.attach(root, child, root)
	var path: String = "res://_netwtest/test_assembly/1/PackedScene.tscn"
	var packed: PackedScene = SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	assert_that(packed).is_not_null()
	assert_that(ResourceLoader.exists(path)).is_true()
	var loaded: PackedScene = load(path) as PackedScene
	assert_that(loaded).is_equal(packed)
	var inst: Node2D = auto_free(loaded.instantiate()) as Node2D
	assert_that(inst.name).is_equal("Root")
	assert_that(inst.get_child(0).name).is_equal("Child")


func test_sync_config_builder() -> void:
	var builder := SyncConfigBuilder.new() \
			.property(
				"Child:position",
				true,
				SyncConfigBuilder.ON_CHANGE,
				true,
				false,
			) \
			.property(
				"Child:modulate",
				true,
				SyncConfigBuilder.ALWAYS,
				false,
				true,
			)
	var cfg: SceneReplicationConfig = auto_free(builder.build())
	var path1: NodePath = NodePath("Child:position")
	assert_that(cfg.has_property(path1)).is_true()
	assert_that(cfg.property_get_spawn(path1)).is_true()
	assert_that(cfg.property_get_watch(path1)).is_true()
	assert_that(cfg.property_get_sync(path1)).is_false()
	var path2: NodePath = NodePath("Child:modulate")
	assert_that(cfg.has_property(path2)).is_true()
	assert_that(cfg.property_get_spawn(path2)).is_true()
	assert_that(cfg.property_get_watch(path2)).is_false()
	assert_that(cfg.property_get_sync(path2)).is_true()


func test_path_namespace_allocates_and_resets() -> void:
	var path1: String = NetwPathNamespace.next_path("category", "hint")
	assert_that(path1.begins_with("res://_netwtest/category/")).is_true()
	assert_that(path1.ends_with("/hint.tscn")).is_true()
	var root: Node2D = auto_free(Node2D.new())
	var packed: PackedScene = SceneAssembly.pack_with_path(root, path1)
	NetwPathNamespace.register_resource(packed)
	assert_that(ResourceLoader.exists(path1)).is_true()
	NetwPathNamespace.reset()
	assert_that(ResourceLoader.exists(path1)).is_false()
	var path2: String = NetwPathNamespace.next_path("category", "hint")
	assert_that(path2.begins_with("res://_netwtest/category/")).is_true()
	assert_that(path2.ends_with("/hint.tscn")).is_true()
	assert_that(path2).is_not_equal(path1)


func test_player_builder_shape() -> void:
	var builder := PlayerBuilder.new("TestPlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_player_sync(
				SyncConfigBuilder.new().property("PlayerSync:position", true),
			)
	assert_that(builder.player_name).is_equal(&"TestPlayer")
	var live: Node2D = auto_free(builder.build()) as Node2D
	assert_that(live.name).is_equal("TestPlayer")
	var spawner: Node = live.get_node("MultiplayerEntity")
	assert_that(spawner).is_not_null()
	assert_that(spawner.owner).is_equal(live)
	var sync_node: Node = live.get_node("PlayerSync")
	assert_that(sync_node).is_not_null()
	assert_that(sync_node.owner).is_equal(live)
	var packed: PackedScene = builder.pack()
	assert_that(builder.packed).is_equal(packed)
	assert_that(builder.resource_path).is_not_empty()
	var inst: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_identical_shape(live, inst)


func test_level_builder_shape() -> void:
	var player_packed := PlayerBuilder.new("MyPlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.pack()
	var marker: Marker2D = auto_free(Marker2D.new())
	marker.name = "MyMarker"
	marker.position = Vector2(50, 50)
	var level_builder := LevelBuilder.new("MyLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_packed]) \
			.with_child(marker)
	assert_that(level_builder.scene_name).is_equal(&"MyLevel")
	var live: Node2D = auto_free(level_builder.build()) as Node2D
	assert_that(live.name).is_equal("MyLevel")
	var spawner: Node = live.get_node("PlayerSpawner")
	assert_that(spawner).is_not_null()
	assert_that(spawner.owner).is_equal(live)
	var child_marker: Node = live.get_node("MyMarker")
	assert_that(child_marker).is_not_null()
	assert_that(child_marker.owner).is_equal(live)
	var packed: PackedScene = level_builder.pack()
	assert_that(level_builder.packed).is_equal(packed)
	assert_that(level_builder.resource_path).is_not_empty()
	var inst: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_identical_shape(live, inst)


func test_player_builder_snapshot_vs_real_tscn() -> void:
	var builder: PlayerBuilder = PlayerBuilder.new("TestPlayerMinimal").with_root(Node2D)
	var _r: PlayerBuilder = builder.with_multiplayer_entity()
	var packed: PackedScene = builder.pack()
	var real_scene: Node2D = auto_free(
		MINIMAL_PLAYER_TSCN.instantiate(),
	) as Node2D
	var built_scene: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_scenes_match(real_scene, built_scene)


func test_player_with_save_snapshot_vs_real_tscn() -> void:
	var db_resource := preload("res://tests/test_db.tres")
	var builder := PlayerBuilder.new("TestPlayerWithSave") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_save(db_resource, &"players_save") \
			.with_player_sync(
				SyncConfigBuilder.new().property(
					"..:position",
					true,
					SyncConfigBuilder.ON_CHANGE,
					true,
					false,
				),
			)
	var packed: PackedScene = builder.pack()
	var real_scene: Node2D = auto_free(
		TEST_PLAYER_WITH_SAVE_TSCN.instantiate(),
	) as Node2D
	var built_scene: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_scenes_match(real_scene, built_scene)


func test_level_builder_snapshot_vs_real_tscn() -> void:
	var db_resource := preload("res://tests/test_db.tres")
	var player_packed := PlayerBuilder.new("TestPlayerFull") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_save(db_resource, &"player") \
			.with_tp("uid://bhif5a1uatdsl", "PlayerSpawner") \
			.with_player_sync(
				SyncConfigBuilder.new().property(
					"..:position",
					true,
					SyncConfigBuilder.ON_CHANGE,
					true,
					false,
				),
			) \
			.pack()
	var template_instance := player_packed.instantiate()

	var level_builder := LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_packed, MINIMAL_PLAYER_TSCN]) \
			.with_child(template_instance)
	var packed: PackedScene = level_builder.pack()
	template_instance.free()

	var real_scene: Node2D = auto_free(
		TEST_LEVEL_TSCN.instantiate(),
	) as Node2D
	var built_scene: Node2D = auto_free(packed.instantiate()) as Node2D

	_assert_scenes_match(real_scene, built_scene)


func test_builders_support_custom_root_types() -> void:
	var p_builder: PlayerBuilder = PlayerBuilder.new("3DPlayer").with_root(Node3D)
	var p_node: Node = auto_free(p_builder.build()) as Node
	assert_that(p_node is Node3D).is_true()
	assert_that(p_node.name).is_equal("3DPlayer")

	var l_builder: LevelBuilder = LevelBuilder.new("3DLevel").with_root(Node3D)
	var l_node: Node = auto_free(l_builder.build()) as Node
	assert_that(l_node is Node3D).is_true()
	assert_that(l_node.name).is_equal("3DLevel")


func test_builders_autogen_and_reset_naming() -> void:
	# Test autogenerated naming
	var l_builder1 := LevelBuilder.new()
	var p_builder1 := PlayerBuilder.new()
	assert_that(l_builder1.scene_name).is_not_equal("")
	assert_that(p_builder1.player_name).is_not_equal("")

	# Verify sequence
	var l_builder2 := LevelBuilder.new()
	var p_builder2 := PlayerBuilder.new()
	assert_that(
		l_builder2.scene_name,
	).is_not_equal(l_builder1.scene_name)
	assert_that(
		p_builder2.player_name,
	).is_not_equal(p_builder1.player_name)

	# Test explicit naming overrides autogen
	var l_builder_explicit := LevelBuilder.new("ExplicitLevel")
	var p_builder_explicit := PlayerBuilder.new("ExplicitPlayer")
	assert_that(l_builder_explicit.scene_name).is_equal(&"ExplicitLevel")
	assert_that(p_builder_explicit.player_name).is_equal(&"ExplicitPlayer")

	# Test that reset resets counter for determinism
	NetwPathNamespace.reset()
	var l_builder_reset := LevelBuilder.new()
	var p_builder_reset := PlayerBuilder.new()
	assert_that(l_builder_reset.scene_name).is_equal(&"AutogenLevel_1")
	assert_that(p_builder_reset.player_name).is_equal(&"AutogenPlayer_1")


func _assert_scenes_match(node1: Node, node2: Node) -> void:
	assert_that(node1.name).is_equal(node2.name)
	assert_that(node1.get_class()).is_equal(node2.get_class())
	assert_that(node1.get_script()).is_equal(node2.get_script())

	if node1 is MultiplayerSynchronizer:
		var sync1 := node1 as MultiplayerSynchronizer
		var sync2 := node2 as MultiplayerSynchronizer
		_assert_configs_match(sync1.replication_config, sync2.replication_config)

	if node1 is MultiplayerSpawner:
		var sp1 := node1 as MultiplayerSpawner
		var sp2 := node2 as MultiplayerSpawner
		assert_that(sp2.spawn_path).is_equal(sp1.spawn_path)
		var scenes1 := sp1.get("_spawnable_scenes") as PackedStringArray
		var scenes2 := sp2.get("_spawnable_scenes") as PackedStringArray
		assert_that(scenes2.size()).is_equal(scenes1.size())

	if (
			node1.get_class() == "MultiplayerEntity"
			or node2.get_class() == "MultiplayerEntity"
	):
		assert_that(node2.get("initial_controller")) \
				.is_equal(node1.get("initial_controller"))

	if node1.get_class() == "SaveComponent" or node2.get_class() == "SaveComponent":
		assert_that(node2.get("database")).is_equal(node1.get("database"))
		assert_that(node2.get("table_name")).is_equal(node1.get("table_name"))

	assert_that(node2.get_child_count()).is_equal(node1.get_child_count())
	for child1 in node1.get_children():
		var child2 := node2.get_node_or_null(NodePath(child1.name))
		assert_that(child2).is_not_null()
		_assert_scenes_match(child1, child2)


func _assert_configs_match(
		cfg_real: SceneReplicationConfig,
		cfg_built: SceneReplicationConfig,
) -> void:
	if cfg_real == null or cfg_built == null:
		assert_that(cfg_real).is_equal(cfg_built)
		return
	var real_props := cfg_real.get_properties()
	var built_props := cfg_built.get_properties()
	assert_that(built_props.size()).is_equal(real_props.size())
	for prop in real_props:
		assert_that(cfg_built.has_property(prop)).is_true()
		assert_that(
			cfg_built.property_get_replication_mode(prop),
		).is_equal(cfg_real.property_get_replication_mode(prop))
		assert_that(
			cfg_built.property_get_spawn(prop),
		).is_equal(cfg_real.property_get_spawn(prop))
		assert_that(
			cfg_built.property_get_watch(prop),
		).is_equal(cfg_real.property_get_watch(prop))
		assert_that(
			cfg_built.property_get_sync(prop),
		).is_equal(cfg_real.property_get_sync(prop))


func test_player_builder_with_custom_synchronizer() -> void:
	var sync_node := MultiplayerSynchronizer.new()
	sync_node.name = "MyCustomSync"

	var builder := PlayerBuilder.new("CustomSyncPlayer") \
			.with_root(Node2D) \
			.with_synchronizer(sync_node, "Components/Nested")

	var live: Node2D = auto_free(builder.build()) as Node2D
	assert_that(live.name).is_equal("CustomSyncPlayer")

	var components_node: Node = live.get_node("Components")
	assert_that(components_node).is_not_null()
	assert_that(components_node.owner).is_equal(live)

	var nested_node: Node = components_node.get_node("Nested")
	assert_that(nested_node).is_not_null()
	assert_that(nested_node.owner).is_equal(live)

	var sync_found: MultiplayerSynchronizer = \
			nested_node.get_node("MyCustomSync") as MultiplayerSynchronizer
	assert_that(sync_found).is_equal(sync_node)
	assert_that(sync_found.owner).is_equal(live)
	assert_that(sync_found.root_path).is_equal(NodePath("../../.."))

	var packed: PackedScene = builder.pack()
	assert_that(packed).is_not_null()

	var inst: Node2D = auto_free(packed.instantiate()) as Node2D
	assert_that(inst.name).is_equal("CustomSyncPlayer")

	var inst_sync: MultiplayerSynchronizer = \
			inst.get_node("Components/Nested/MyCustomSync") as MultiplayerSynchronizer
	assert_that(inst_sync).is_not_null()
	assert_that(inst_sync.root_path).is_equal(NodePath("../../.."))


func _assert_identical_shape(node1: Node, node2: Node) -> void:
	assert_that(node1.name).is_equal(node2.name)
	assert_that(node1.get_class()).is_equal(node2.get_class())
	assert_that(node1.get_script()).is_equal(node2.get_script())
	assert_that(node1.get_child_count()).is_equal(node2.get_child_count())
	for i in range(node1.get_child_count()):
		_assert_identical_shape(node1.get_child(i), node2.get_child(i))
