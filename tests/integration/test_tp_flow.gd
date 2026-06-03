## Tests [TPComponent] teleportation with real multiplayer peers.
##
## Covers the full [method MultiplayerTree.request_join_player] RPC join chain
## and cross-scene teleportation.
class_name TestTPFlow
extends NetwTestSuite

## Node path from level root to the [SpawnerComponent] spawner.
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder
var level_2_builder: LevelBuilder


func before_test() -> void:
	test_dir = create_temp_dir("tp_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	var player_path := NetwPathNamespace.next_path("player", "TestPlayerFull")
	var level_path := NetwPathNamespace.next_path("level", "TestLevel")
	var level_2_path := NetwPathNamespace.next_path("level", "TestLevel2")

	player_builder = PlayerBuilder.new("TestPlayerFull") \
			.with_root(Node2D) \
			.with_spawner() \
			.with_save(db, &"players") \
			.with_tp(level_path, "PlayerSpawner") \
			.with_player_sync(
				SyncConfigBuilder.new().property("..:position", true),
			)
	player_builder.pack(player_path)

	var template_instance: Node = player_builder.packed.instantiate()

	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack(level_path)

	var marker: Marker2D = Marker2D.new()
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
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)
	harness.register_spawnable_scene(level_2_builder.packed)

	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


## Joins a player via the real RPC chain and overrides its database.
func _spawn_tp_player(
		scene_path: String,
		client: MultiplayerTree = null,
) -> Node2D:
	if client == null:
		client = client0
	var player := await harness.join_player(
		client,
		scene_path,
		SPAWNER_PATH,
	) as Node2D

	_set_player_database(player)
	return player


func _set_player_database(player: Node) -> void:
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	if save_comp:
		save_comp.database = db
		save_comp.table_name = &"players"
		NetwEntity.of(player).contribute_save_property(player, &"position", &"position")


func _tp_target(scene_path: String, node_path: String) -> SceneNodePath:
	var target := SceneNodePath.new()
	target.scene_path = scene_path
	target.node_path = node_path
	return target


func _await_tp(promise: TPComponent.TeleportPromise) -> void:
	var timeout_timer := get_tree().create_timer(DEFAULT_TIMEOUT)
	while not promise.is_completed:
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			fail(
				"Timed out waiting for teleport completion after %.1f seconds."
				% DEFAULT_TIMEOUT,
			)
			return


func test_two_scenes_spawned() -> void:
	var server_mgr := harness.server_scene_manager()
	assert_that(server_mgr.active_scenes.size()).is_equal(2)


func test_tp_spawn_places_in_correct_scene() -> void:
	var player := await _spawn_tp_player(level_builder.resource_path)

	var server_mgr := harness.server_scene_manager()
	var scene: MultiplayerScene = server_mgr.active_scenes.get(level_builder.scene_name)
	assert_that(player.get_parent()).is_equal(scene.level)


func test_reparent_moves_player_between_scenes() -> void:
	var server_player := await _spawn_tp_player(level_builder.resource_path)
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D

	_set_player_database(client_player)

	var server_mgr := harness.server_scene_manager()
	var scene2: MultiplayerScene = server_mgr.active_scenes.get(level_2_builder.scene_name)

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await _await_tp(
		client_tp.teleport(
			_tp_target(level_2_builder.resource_path, "TPTarget"),
		),
	)

	assert_that(server_player.get_parent()).is_equal(scene2.level)


func test_teleported_snaps_to_marker() -> void:
	await _spawn_tp_player(level_builder.resource_path)
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D

	_set_player_database(client_player)

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await _await_tp(
		client_tp.teleport(
			_tp_target(level_2_builder.resource_path, "TPTarget"),
		),
	)

	var client_player2 := await harness.wait_for_player(
		client0,
		level_2_builder.scene_name,
	) as Node2D
	assert_that(client_player2.global_position).is_equal(Vector2(100, 100))
