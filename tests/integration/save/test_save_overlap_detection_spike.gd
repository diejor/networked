## SPIKE — DELETE AFTER READING THE FINDING.
##
## Throwaway probe for the SaveComponent security refactor (per-property trust).
## It answers the one question that source-reading could not settle: at the
## candidate overlap-detection point (post-[code]_ready[/code]), are sibling
## [ProxySynchronizer] real paths populated so overlap detection can see that a
## save-tracked property is ALSO governed by another synchronizer — AND does that
## still hold after a teleport reparent re-fires [code]_ready[/code] on the whole
## subtree via [code]TPComponent._request_ready_recursive[/code]?
##
## What reading already settled (NOT re-proved here):
##   - Bundling does not hide payload columns: get_real_path/get_virtual_properties
##     still expose them (PackedSynchronizer.finalize only flips the WIRE config to
##     NEVER). So overlap detection reads get_real_path, not the config.
##   - `spawning` is too early: MultiplayerEntity emits it inside
##     _on_owner_tree_entered, BEFORE sibling synchronizers' _ready (where
##     ProxySynchronizer.finalize runs). Detection must be a post-_ready pass.
##
## What this spike pins at runtime:
##   - Overlap resolves at a post-_ready point and survives a teleport reparent.
##   - The authority divergence that makes "read the source node's authority"
##     unsafe: the StateSynchronizer governing `position` is authority 1 (server),
##     while the body node carries the controller's (client) authority. A
##     source-node check would misclassify a server-owned property as client-owned.
class_name TestSaveOverlapDetectionSpike
extends NetwTestSuite

const SPAWNER_PATH := "OverlapPlayer/MultiplayerEntity"

var harness: NetwTestHarness
var client0: MultiplayerTree
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder
var level_2_builder: LevelBuilder


func before_test() -> void:
	db = auto_free(NetwDatabase.new())
	# Dict backend: no FileSystemBackend path-registry collision when the packed
	# scene embeds (duplicates) the database resource.
	db.backend = NetwDatabaseBackend.Dict.new()

	var player_path := NetwPathNamespace.next_path("player", "OverlapPlayer")
	var level_path := NetwPathNamespace.next_path("level", "TestLevel")
	var level_2_path := NetwPathNamespace.next_path("level", "TestLevel2")

	# SaveComponent tracks `position`; the StateSynchronizer ALSO governs
	# `position` (authority 1) — the overlap case. with_state now bakes a packable
	# real-path config, so the payload survives the harness join (pack/instantiate).
	player_builder = PlayerBuilder.new("OverlapPlayer") \
			.with_root(Node2D) \
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
	# StateSynchronizer._ready resolves a required LagCompensation on the server.
	harness.add_lag_compensation()
	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func _spawn_player(scene_path: String) -> Node2D:
	return await harness.join_player(client0, scene_path, SPAWNER_PATH) as Node2D


# Resolves a ProxySynchronizer's virtual property to its live [object, sub_path].
# This is the candidate overlap key: two synchronizers overlap when they resolve
# to the same object instance and the same sub-path.
func _resolve_target(sync: ProxySynchronizer, vname: StringName) -> Array:
	var root := sync.get_node_or_null(sync.root_path)
	if not root:
		return []
	var path := sync.get_real_path(vname)
	if path.is_empty():
		return []
	var rr := root.get_node_and_resource(path)
	return [rr[0], rr[2]]


func _assert_overlap(player: Node) -> void:
	var save: SaveComponent = player.get_node("%SaveComponent")
	var state := player.get_node("StateSync") as StateSynchronizer

	# Both siblings finalized: real paths are populated post-_ready.
	assert_bool(save.get_real_path(&"position").is_empty()).is_false()
	assert_bool(state.get_real_path(&"position").is_empty()).is_false()

	# Overlap resolves: same object instance + same sub-path.
	var save_t := _resolve_target(save, &"position")
	var state_t := _resolve_target(state, &"position")
	assert_bool(save_t.is_empty()).is_false()
	assert_bool(state_t.is_empty()).is_false()
	assert_object(save_t[0]).is_same(state_t[0])
	assert_that(save_t[1]).is_equal(state_t[1])


func test_overlap_resolves_at_post_ready() -> void:
	var player := await _spawn_player(level_builder.resource_path)
	# Stand-in for the production "post-_ready deferred" detection point.
	await get_tree().process_frame

	_assert_overlap(player)

	# Authority divergence: the governing synchronizer is server (1), the body
	# carries the controller's (client) authority. Reading the SOURCE node's
	# authority would therefore misclassify `position` as client-trusted.
	var state := player.get_node("StateSync") as StateSynchronizer
	assert_int(state.get_multiplayer_authority()).is_equal(1)
	assert_int(player.get_multiplayer_authority()).is_not_equal(1)


func test_overlap_survives_teleport_reparent() -> void:
	var server_player := await _spawn_player(level_builder.resource_path)
	var client_player := await harness.wait_for_player(
		client0, level_builder.scene_name,
	) as Node2D

	var target := SceneNodePath.new()
	target.scene_path = level_2_builder.resource_path
	target.node_path = "TPTarget"

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	var promise := client_tp.teleport(target)
	@warning_ignore("redundant_await")
	await assert_signal(promise).wait_until(1000).is_emitted("completed")

	# Server reparented the same node (request_ready re-fired _ready on the subtree).
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
