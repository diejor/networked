## Integration coverage for manager-free SINGLE scene authoring.
class_name TestBareSceneAdoption
extends NetwTestSuite

var harness: NetwTestHarness
var level_builder: LevelBuilder


func before_test() -> void:
	level_builder = LevelBuilder.new("BareLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	harness = make_harness()
	await harness.setup(level_builder.packed)


func test_direct_level_becomes_a_single_scene_declaration() -> void:
	var server := harness.server()
	var manager := server.get_service(MultiplayerSceneManager) \
			as MultiplayerSceneManager

	assert_object(manager).is_not_null()
	assert_array(manager.get_configured_paths()).contains(
		[level_builder.resource_path],
	)
	assert_object(
		server.get_node_or_null(NodePath(level_builder.scene_name)),
	).is_null()


func test_direct_level_spawns_through_a_plain_wrapper() -> void:
	await harness.add_client()
	var api := harness.server().api
	var container := api.scene(level_builder.scene_name)

	assert_object(container).is_not_null()
	assert_bool(container.level_container() is SubViewport).is_false()
	assert_object(container.level).is_instanceof(Node2D)
