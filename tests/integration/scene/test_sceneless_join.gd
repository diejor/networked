## Integration test for the default-scene join flow.
##
## Verifies that a world scene the harness wraps in an explicit
## [MultiplayerSceneManager] (see [method NetwTestHarness.setup]) routes joins
## and spawns players via the managed scene.
class_name TestLobbylessJoin
extends NetwTestSuite

var harness: NetwTestHarness
var client: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder
var spawner_path: String


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D).with_multiplayer_entity()
	player_builder.pack()

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack()
	template_instance.free()

	spawner_path = player_builder.player_name

	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.register_initial_scene_path(level_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)
	client = await harness.add_client()


func test_default_scene_wraps_level_and_context() -> void:
	var api := harness.server().api
	var scene := api.scene(level_builder.scene_name)
	assert_that(scene).is_not_null()

	var level := scene.level
	assert_that(level).is_not_null()

	var ctx := Netw.of(level)
	assert_that(ctx).is_not_null()
	assert_that(ctx.is_active()).is_true()


func test_player_spawns_in_level_after_join() -> void:
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	var join_payload := harness.make_spawn_payload(
		username,
		level_builder.resource_path,
		spawner_path,
	)

	client.api.session.submit_join(join_payload)

	var rj := ResolvedJoin.new()
	rj.username = username
	rj.peer_id = peer_id
	var player_name := NetwEntity.name_for(rj)
	var api := harness.server().api
	var scene := api.scene(level_builder.scene_name)
	assert_that(scene).is_not_null()
	var level := scene.level

	@warning_ignore("redundant_await")
	await assert_func(level, "get_node_or_null", [player_name]) \
			.wait_until(1000) \
			.is_not_null()

	var player := level.get_node(player_name)
	var client_comp := NetwEntity.of(player)
	assert_that(client_comp).is_not_null()
	assert_that(str(client_comp.entity_id)).is_equal(username)
