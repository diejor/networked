## Regression test for the overlap primitives behind the persistence L1 lint.
##
## [method SynchronizersCache.governed_targets] and
## [method NetwEntity.governs_property] must see that a persisted property is
## ALSO governed by another synchronizer, at a post-[code]_ready[/code] point and
## after a teleport reparents the subtree. The lint is non-load-bearing
## (the config is frozen from the declaration), so this pins only the detection
## primitives, not warning emission.
class_name TestSaveOverlapLint
extends NetwTestSuite

const SPAWNER_PATH := "OverlapPlayer"

var harness: NetwTestHarness
var client0: MultiplayerTree
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder
var level_2_builder: LevelBuilder


func before_test() -> void:
	db = auto_free(NetwDatabase.new())
	# Dict backend: no FileSystemDatabase path-registry collision when the packed
	# scene embeds (duplicates) the database resource.
	db.backend = NetwDatabaseBackend.Dict.new()

	var player_path := NetwPathNamespace.next_path("player", "OverlapPlayer")
	var level_path := NetwPathNamespace.next_path("level", "TestLevel")
	var level_2_path := NetwPathNamespace.next_path("level", "TestLevel2")

	# Persistence tracks `position`; the root's derived state set ALSO governs
	# `position` (server-authored) -- the overlap case. The marks live on the root
	# script, so the declaration survives the harness join (pack/instantiate).
	player_builder = PlayerBuilder.new("OverlapPlayer") \
			.with_root(StateSyncBody) \
			.with_multiplayer_entity() \
			.with_save(db, &"overlap") \
			.with_save_property(&"position") \
			.with_state([&"position"]) \
			.with_tp(level_path, "PlayerSpawner")
	player_builder.pack(player_path)

	var template_instance: Node = player_builder.packed.instantiate()

	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack(level_path)

	var marker := Marker2D.new()
	marker.name = "TPTarget"
	marker.position = Vector2(100, 100)

	level_2_builder = LevelBuilder.new("TestLevel2") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance) \
			.with_child(marker)
	level_2_builder.pack(level_2_path)

	template_instance.free()
	marker.free()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)
	harness.register_spawnable_scene(level_2_builder.packed)
	# The state set's timeline registration resolves LagCompensation on the server.
	harness.add_lag_compensation()
	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func _spawn_player(scene_path: String) -> Node2D:
	return await harness.join_player(client0, scene_path, SPAWNER_PATH) as Node2D


func _assert_overlap(player: Node) -> void:
	var entity := NetwEntity.of(player)

	# The root script's derived state set governs the same live target as the
	# persisted position column.
	var binding := entity.state_binding
	assert_that(binding).is_not_null()
	assert_bool(&"position" in binding.set.keys()).is_true()

	# The entity-level accessor the lint runs on finds the derived set that
	# governs the persisted column's live target.
	var save_path := entity.property_path(player, &"position")
	assert_bool(entity.governs_property(save_path)).is_true()


func test_overlap_resolves_at_post_ready() -> void:
	var player := await _spawn_player(level_builder.resource_path)
	# Stand-in for the production "post-_ready deferred" detection point.
	await get_tree().process_frame

	_assert_overlap(player)

	# Authority divergence: the state stream's trust comes from the set's write
	# policy, while the body carries the controller's (client) node authority.
	# Reading the SOURCE node's authority would therefore misclassify `position`
	# as client-trusted.
	var entity := NetwEntity.of(player)
	assert_int(entity.state_binding.set.policy) \
			.is_equal(NetwScriptModel.Policy.AUTHORITY)
	assert_int(player.get_multiplayer_authority()).is_not_equal(1)


func test_overlap_survives_teleport_reparent() -> void:
	var server_player := await _spawn_player(level_builder.resource_path)
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D

	var target := SceneNodePath.new()
	target.scene_path = level_2_builder.resource_path
	target.node_path = "TPTarget"

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	var promise := client_tp.teleport(target)
	@warning_ignore("redundant_await")
	await assert_signal(promise).wait_until(1000).is_emitted("completed")

	# Server reparented the same node.
	var scene2: MultiplayerScene = harness.server_scene_manager() \
			.active_scenes.get(level_2_builder.scene_name)
	assert_object(server_player.get_parent()).is_same(scene2.level)
	await get_tree().process_frame

	# The sibling re-finalized after the reparent: overlap still resolves.
	_assert_overlap(server_player)

	# Drain the server's post-commit AreaReparentGuard await so teardown does not
	# strand the suspended coroutine (mirrors test_tp_flow).
	var server_tp: TPComponent = server_player.get_node("%TPComponent")
	for _i in 8:
		if server_tp._tp_guard == null:
			break
		await get_tree().physics_frame
