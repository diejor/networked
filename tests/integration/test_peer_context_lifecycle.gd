## Integration tests for [NetwPeerContext] lifecycle.
class_name TestPeerContextLifecycle
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayerWithSave/SpawnerComponent"

var harness: NetwTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	test_dir = create_temp_dir("peer_context_lifecycle")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend
	
	player_builder = PlayerBuilder.new("TestPlayerWithSave") \
		.with_root(Node2D) \
		.with_spawner() \
		.with_save(db, &"players_save") \
		.with_player_sync(
			SyncConfigBuilder.new().property("..:position", true)
		)
	player_builder.pack()
	
	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new("TestLevelSave") \
		.with_root(Node2D) \
		.with_multiplayer_spawner("..", [player_builder.packed]) \
		.with_child(template_instance)
	level_builder.pack()
	template_instance.free()
	
	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	
	harness.register_spawnable_scene(level_builder.packed)
	
	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func test_context_erased_on_peer_disconnect() -> void:
	var server := harness.server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()
	
	server.get_peer_context(client_peer_id)
	assert_that(server.has_peer_context(client_peer_id)).is_true()
	
	client0.multiplayer_peer.close()
	@warning_ignore("redundant_await")
	await assert_func(server, "has_peer_context", [client_peer_id]) \
		.wait_until(1000) \
		.is_false()


func _spawn_save_player() -> void:
	var player: Node2D = await harness.join_player(
		client0, level_builder.resource_path, SPAWNER_PATH)
	
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.database = db
	save_comp.table_name = &"players"
	NetwEntity.of(player) \
		.contribute_save_property(player, &"position", &"position")
	
	await harness.wait_for_player(client0, level_builder.scene_name)


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
