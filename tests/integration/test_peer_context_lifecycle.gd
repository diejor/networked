## Integration tests for [NetwPeerContext] lifecycle.
class_name TestPeerContextLifecycle
extends NetwTestSuite

const PlayerBuilder := preload(
	"res://addons/networked_test/builders/player_builder.gd"
)
const LevelBuilder := preload(
	"res://addons/networked_test/builders/level_builder.gd"
)
const SPAWNER_PATH := "TestPlayerWithSave/SpawnerComponent"
const SCENE_NAME := &"TestLevelSave"

var harness: NetwTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase
var player_packed: PackedScene
var level_packed: PackedScene


func before_test() -> void:
	test_dir = create_temp_dir("peer_context_lifecycle")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	var player_builder: PlayerBuilder = PlayerBuilder.new("TestPlayerWithSave")
	var _r1: PlayerBuilder = player_builder.with_spawner()
	var _r2: PlayerBuilder = player_builder.with_save(db, &"players_save")
	player_packed = player_builder.pack()

	var template_instance: Node = player_packed.instantiate()
	var level_builder: LevelBuilder = LevelBuilder.new("TestLevelSave")
	var _r3: LevelBuilder = level_builder.with_multiplayer_spawner(
		"..", [player_packed]
	)
	var _r4: LevelBuilder = level_builder.with_child(template_instance)
	level_packed = level_builder.pack()
	template_instance.free()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_packed)

	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()


func test_context_erased_on_peer_disconnect() -> void:
	var server := harness.server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()

	server.get_peer_context(client_peer_id)
	assert_that(server.has_peer_context(client_peer_id)).is_true()

	client0.multiplayer_peer.close()
	await wait_until(
		func(): return not server.has_peer_context(client_peer_id))

	assert_that(server.has_peer_context(client_peer_id)).is_false()


func _spawn_save_player() -> void:
	var player: Node2D = await harness.join_player(
		client0, level_packed.resource_path, SPAWNER_PATH)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.database = db
	save_comp.table_name = &"players"

	await harness.wait_for_player(client0, SCENE_NAME)


func test_server_context_does_not_contain_client_peer_id() -> void:
	await _spawn_save_player()

	var server := harness.server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()

	# The server should have no context keyed by the client's peer_id.
	assert_that(server.has_peer_context(client_peer_id)).is_false()


func test_client_context_does_not_contain_server_peer_id() -> void:
	await _spawn_save_player()

	# The client should have no context keyed by the server's peer_id (1).
	assert_that(client0.has_peer_context(1)).is_false()


func test_save_buckets_contain_no_shared_component_instances() -> void:
	await _spawn_save_player()

	var server := harness.server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()

	var server_bucket := server \
		.get_peer_context(1) \
		.get_bucket(SaveComponent.Bucket) as SaveComponent.Bucket

	var client_bucket := client0 \
		.get_peer_context(client_peer_id) \
		.get_bucket(SaveComponent.Bucket) as SaveComponent.Bucket

	assert_that(server_bucket.registered).is_not_empty()
	assert_that(client_bucket.registered).is_not_empty()

	for comp in server_bucket.registered:
		assert_that(client_bucket.registered.has(comp)).is_false()
