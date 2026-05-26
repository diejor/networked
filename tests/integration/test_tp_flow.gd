## Tests [TPComponent] teleportation with real multiplayer peers.
##
## Covers the full [method MultiplayerTree.request_join_player] RPC join chain
## and cross-scene teleportation.
class_name TestTPFlow
extends NetwTestSuite

const PlayerBuilder := preload(
	"res://addons/networked_test/builders/player_builder.gd"
)
const LevelBuilder := preload(
	"res://addons/networked_test/builders/level_builder.gd"
)
const NetwPathNamespace := preload(
	"res://addons/networked_test/builders/path_namespace.gd"
)

## Node path from level root to the [SpawnerComponent] spawner.
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase
var player_packed: PackedScene
var level_packed: PackedScene
var level_2_packed: PackedScene


func before_test() -> void:
	test_dir = create_temp_dir("tp_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	var player_path := NetwPathNamespace.next_path("player", "TestPlayerFull")
	var level_path := NetwPathNamespace.next_path("level", "TestLevel")
	var level_2_path := NetwPathNamespace.next_path("level", "TestLevel2")

	var player_builder: PlayerBuilder = PlayerBuilder.new("TestPlayerFull")
	var _r1: PlayerBuilder = player_builder.with_spawner()
	var _r2: PlayerBuilder = player_builder.with_save(db, &"players")
	var _r3: PlayerBuilder = player_builder.with_tp(level_path, "PlayerSpawner")
	player_packed = player_builder.pack(player_path)

	var template_instance: Node = player_packed.instantiate()

	var level_builder1: LevelBuilder = LevelBuilder.new("TestLevel")
	var _r4: LevelBuilder = level_builder1.with_multiplayer_spawner(
		"..", [player_packed]
	)
	var _r5: LevelBuilder = level_builder1.with_child(template_instance)
	level_packed = level_builder1.pack(level_path)

	var marker: Marker2D = Marker2D.new()
	marker.name = "TPTarget"
	marker.position = Vector2(100, 100)

	var level_builder2: LevelBuilder = LevelBuilder.new("TestLevel2")
	var _r6: LevelBuilder = level_builder2.with_multiplayer_spawner(
		"..", [player_packed]
	)
	var _r7: LevelBuilder = level_builder2.with_child(template_instance)
	var _r8: LevelBuilder = level_builder2.with_child(marker)
	level_2_packed = level_builder2.pack(level_2_path)

	template_instance.free()
	marker.free()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_packed)
	harness.register_spawnable_scene(level_2_packed)

	client0 = await harness.add_client()


func after_test() -> void:
	clean_temp_dir()
	if is_instance_valid(harness):
		await harness.teardown()


## Joins a player via the real RPC chain and overrides its database.
func _spawn_tp_player(
	scene_path: String,
	client: MultiplayerTree = null,
) -> Node2D:
	if client == null:
		client = client0
	var player := await harness.join_player(
		client, scene_path, SPAWNER_PATH) as Node2D

	_set_player_database(player)
	return player


func _set_player_database(player: Node) -> void:
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	if save_comp:
		save_comp.database = db
		save_comp.table_name = &"players"


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
					% DEFAULT_TIMEOUT)
			return


func test_two_scenes_spawned() -> void:
	var server_mgr := harness.server_scene_manager()
	assert_that(server_mgr.active_scenes.size()).is_equal(2)


func test_tp_spawn_places_in_correct_scene() -> void:
	var player := await _spawn_tp_player(level_packed.resource_path)

	var server_mgr := harness.server_scene_manager()
	var scene: MultiplayerScene = server_mgr.active_scenes.get(&"TestLevel")
	assert_that(player.get_parent()).is_equal(scene.level)


func test_reparent_moves_player_between_scenes() -> void:
	var server_player := await _spawn_tp_player(level_packed.resource_path)
	var client_player := await harness.wait_for_player(
		client0, &"TestLevel") as Node2D

	_set_player_database(client_player)

	var server_mgr := harness.server_scene_manager()
	var scene2: MultiplayerScene = server_mgr.active_scenes.get(&"TestLevel2")

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await _await_tp(client_tp.teleport(
		_tp_target(level_2_packed.resource_path, "TPTarget")
	))

	assert_that(server_player.get_parent()).is_equal(scene2.level)


func test_teleported_snaps_to_marker() -> void:
	await _spawn_tp_player(level_packed.resource_path)
	var client_player := await harness.wait_for_player(
		client0, &"TestLevel") as Node2D

	_set_player_database(client_player)

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await _await_tp(client_tp.teleport(
		_tp_target(level_2_packed.resource_path, "TPTarget")
	))

	var client_player2 := await harness.wait_for_player(
		client0, &"TestLevel2") as Node2D
	assert_that(client_player2.global_position).is_equal(Vector2(100, 100))
