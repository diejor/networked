## Integration tests for isolation as a per-scene declaration.
##
## Isolation used to be a session-wide axis, so every live scene in a session
## shared one answer. It now rides the spawn recipe, which is what lets one
## session hold a shared lobby and an isolated match world at the same time.
## The rule these tests pin: a hosting peer builds a world-owning [SubViewport]
## only for a scene that declared [constant
## NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD], and every other
## container stays a plain [Node] on every peer.
@tool
class_name TestScenePerSceneIsolation
extends NetwTestSuite

var harness: NetwTestHarness
var server_api: NetwMultiplayer
var client0: MultiplayerTree
var level_builder: LevelBuilder
var probe_builder: PlayerBuilder


func before_test() -> void:
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	probe_builder = PlayerBuilder.new("IsolationProbe").with_root(Node2D) \
			.with_multiplayer_entity()
	probe_builder.pack()

	harness.register_spawnable_scene(level_builder.packed)
	server_api = harness.server().api
	client0 = await harness.add_client()


func test_two_scenes_in_one_session_carry_different_isolation() -> void:
	var shared := server_api.scene_spawn(
		level_builder.resource_path,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE,
	)
	var isolated := server_api.scene_spawn(
		level_builder.resource_path,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
	)
	await drain_frames(get_tree(), 2)

	assert_that(shared).is_not_null()
	assert_that(isolated).is_not_null()
	# The host simulates, so only it builds the world-owning container.
	assert_bool(shared.level_container() is SubViewport).is_false()
	assert_bool(isolated.level_container() is SubViewport).is_true()


func test_a_viewing_peer_builds_a_plain_container_for_an_isolated_scene() -> void:
	var isolated := server_api.scene_spawn(
		level_builder.resource_path,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
	)
	await drain_frames(get_tree(), 2)
	isolated.admit(client0.multiplayer_peer.get_unique_id())

	var mirror := await _wait_for_mirror(isolated.record.route)

	assert_that(mirror).is_not_null()
	assert_bool((mirror as Node) is SubViewport).is_false()


func test_a_nested_spawn_anchors_in_both_isolation_modes() -> void:
	for isolation: int in [
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
	]:
		var handle := server_api.scene_spawn(level_builder.resource_path, isolation)
		await drain_frames(get_tree(), 2)
		handle.admit(client0.multiplayer_peer.get_unique_id())

		var child := probe_builder.packed.instantiate()
		child.name = "Nested%d" % isolation
		var child_entity := harness.server().api._replication.replicate(child)
		handle.level.add_child(child)
		await drain_frames(get_tree(), 4)

		var mirror := await _wait_for_mirror(child_entity.route)
		assert_that(mirror) \
				.override_failure_message(
					"nested spawn did not anchor under isolation %d" % isolation,
				) \
				.is_not_null()


func test_isolation_is_write_once_before_arm() -> void:
	var api := harness.server().api
	var scene := server_api.scene_spawn(
		level_builder.resource_path,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE,
	)
	await drain_frames(get_tree(), 2)
	var live := scene.entity

	assert_that(
		api.scene_set_param(
			live,
			NetwMultiplayer.SceneParam.SCENE_PARAM_ISOLATION,
			NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
		),
	).is_equal(ERR_UNCONFIGURED)


func test_two_instances_of_one_level_own_distinct_layers() -> void:
	var first_scene := server_api.scene_spawn(level_builder.resource_path)
	var second_scene := server_api.scene_spawn(level_builder.resource_path)
	await drain_frames(get_tree(), 2)

	assert_that(first_scene).is_not_null()
	assert_that(second_scene).is_not_null()
	# Same stem, two admission boundaries. Keying the layer by name alone used
	# to collapse these into one, so two arenas silently shared visibility.
	assert_that(first_scene.layer).is_not_null()
	assert_that(second_scene.layer).is_not_null()
	assert_that(first_scene.layer).is_not_equal(second_scene.layer)

	# The harness already brought up one instance on startup, so the two spawned
	# here make three live instances of the one stem.
	var stem := StringName(level_builder.scene_name)
	assert_int(server_api.scene_instances(stem).size()).is_equal(3)



func test_a_manager_less_harness_declares_and_spawns_a_scene() -> void:
	# The scene manager is inspector sugar, not a requirement: a session owns a
	# registry, so declaring and spawning must work with no manager node at all.
	var h := make_unmanaged_harness()
	await h.setup()
	h.register_spawnable_scene(level_builder.packed)

	var scene := h.server().api.scene_spawn(level_builder.resource_path)
	await drain_frames(get_tree(), 2)

	assert_that(h.server().api.get_service(MultiplayerSceneManager)).is_null()
	assert_that(scene).is_not_null()
	assert_int(h.server().api.scene_list().size()).is_greater_equal(1)
	await h.teardown()


func _wait_for_mirror(route: int, frames: int = 120) -> Node:
	for i in frames:
		if client0.api.entity_get_state(client0.api.entity_from_route(route)) \
				== NetwMultiplayer.EntityState.LIVE:
			return client0.api.entity_get_node(client0.api.entity_from_route(route))
		await get_tree().process_frame
	return null
