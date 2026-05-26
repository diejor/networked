## Unit tests for programmatic builder primitives.
##
## Hard-wrapped to 80 columns.
class_name TestBuilders
extends NetwTestSuite

const SceneAssembly := preload(
	"res://addons/networked_test/builders/scene_assembly.gd"
)
const SyncConfigBuilder := preload(
	"res://addons/networked_test/builders/sync_config_builder.gd"
)
const NetwPathNamespace := preload(
	"res://addons/networked_test/builders/path_namespace.gd"
)
const PlayerBuilder := preload(
	"res://addons/networked_test/builders/player_builder.gd"
)
const LevelBuilder := preload(
	"res://addons/networked_test/builders/level_builder.gd"
)
const MINIMAL_PLAYER_TSCN := preload(
	"res://addons/networked_test/fixtures/TestPlayerMinimal.tscn"
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
	var builder: SyncConfigBuilder = SyncConfigBuilder.new()
	var _r1: SyncConfigBuilder = builder.property(
		"Child:position", true, SyncConfigBuilder.ON_CHANGE, true, false
	)
	var _r2: SyncConfigBuilder = builder.property(
		"Child:modulate", true, SyncConfigBuilder.ALWAYS, false, true
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
	var builder: PlayerBuilder = PlayerBuilder.new("TestPlayer")
	var _r1: PlayerBuilder = builder.with_spawner()
	var _r2: PlayerBuilder = builder.with_player_sync(
		SyncConfigBuilder.new().property("PlayerSync:position", true)
	)
	var live: Node2D = auto_free(builder.build())
	assert_that(live.name).is_equal("TestPlayer")
	var spawner: Node = live.get_node("SpawnerComponent")
	assert_that(spawner).is_not_null()
	assert_that(spawner.owner).is_equal(live)
	var sync_node: Node = live.get_node("PlayerSync")
	assert_that(sync_node).is_not_null()
	assert_that(sync_node.owner).is_equal(live)
	var packed: PackedScene = builder.pack()
	var inst: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_identical_shape(live, inst)


func test_level_builder_shape() -> void:
	var player_builder: PlayerBuilder = PlayerBuilder.new("MyPlayer")
	var _r1: PlayerBuilder = player_builder.with_spawner()
	var player_packed: PackedScene = player_builder.pack()
	var marker: Marker2D = auto_free(Marker2D.new())
	marker.name = "MyMarker"
	marker.position = Vector2(50, 50)
	var level_builder: LevelBuilder = LevelBuilder.new("MyLevel")
	var _r2: LevelBuilder = level_builder.with_multiplayer_spawner(
		"..", [player_packed]
	)
	var _r3: LevelBuilder = level_builder.with_child(marker)
	var live: Node2D = auto_free(level_builder.build())
	assert_that(live.name).is_equal("MyLevel")
	var spawner: Node = live.get_node("PlayerSpawner")
	assert_that(spawner).is_not_null()
	assert_that(spawner.owner).is_equal(live)
	var child_marker: Node = live.get_node("MyMarker")
	assert_that(child_marker).is_not_null()
	assert_that(child_marker.owner).is_equal(live)
	var packed: PackedScene = level_builder.pack()
	var inst: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_identical_shape(live, inst)


func test_player_builder_snapshot_vs_real_tscn() -> void:
	var builder: PlayerBuilder = PlayerBuilder.new("TestPlayerMinimal")
	var _r: PlayerBuilder = builder.with_spawner()
	var packed: PackedScene = builder.pack()
	var real_scene: Node2D = auto_free(
		MINIMAL_PLAYER_TSCN.instantiate()
	) as Node2D
	var built_scene: Node2D = auto_free(packed.instantiate()) as Node2D
	_assert_identical_shape(real_scene, built_scene)
	var real_spawner: Node = real_scene.get_node("SpawnerComponent")
	var built_spawner: Node = built_scene.get_node("SpawnerComponent")
	assert_that(built_spawner.get_script()).is_equal(real_spawner.get_script())
	assert_that(
		built_spawner.get("authority_mode")
	).is_equal(real_spawner.get("authority_mode"))


func test_builders_support_custom_root_types() -> void:
	var base_3d: Node3D = auto_free(Node3D.new()) as Node3D
	var p_builder: PlayerBuilder = PlayerBuilder.new("3DPlayer", base_3d)
	var p_node: Node = auto_free(p_builder.build()) as Node
	assert_that(p_node is Node3D).is_true()
	assert_that(p_node.name).is_equal("3DPlayer")

	var l_builder: LevelBuilder = LevelBuilder.new("3DLevel", base_3d)
	var l_node: Node = auto_free(l_builder.build()) as Node
	assert_that(l_node is Node3D).is_true()
	assert_that(l_node.name).is_equal("3DLevel")


func _assert_identical_shape(node1: Node, node2: Node) -> void:
	assert_that(node1.name).is_equal(node2.name)
	assert_that(node1.get_class()).is_equal(node2.get_class())
	assert_that(node1.get_child_count()).is_equal(node2.get_child_count())
	for i in range(node1.get_child_count()):
		_assert_identical_shape(node1.get_child(i), node2.get_child(i))
