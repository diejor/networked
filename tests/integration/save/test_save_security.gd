## Server-authoritative persistence security tests.
##
## The database only ever sees what the server sees. Persistence adds no wire
## format: every flush reads server-side live values, so there is no client to
## server channel to forge. A persisted field governed by a synchronizer is
## server-authoritative, and the L1 lint primitive detects that overlap.
class_name TestSaveSecurity
extends NetwTestSuite

const SPAWNER_PATH := "SecurityPlayer"

var harness: NetwTestHarness
var client0: MultiplayerTree
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	db = auto_free(NetwDatabase.new())
	db.backend = NetwDatabaseBackendDict.new()

	var player_path := NetwPathNamespace.next_path("player", "SecurityPlayer")
	var level_path := NetwPathNamespace.next_path("level", "SecurityLevel")

	# position: persisted AND governed by the derived state set (the real authority).
	player_builder = PlayerBuilder.new("SecurityPlayer") \
			.with_root(StateSyncBody) \
			.with_multiplayer_entity() \
			.with_save(db, &"security") \
			.with_save_property(&"position") \
			.with_state([&"position"])
	player_builder.pack(player_path)

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new("SecurityLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack(level_path)
	template_instance.free()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)
	harness.add_lag_compensation()
	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func _spawn_player() -> Node2D:
	var player := await harness.join_player(
		client0,
		level_builder.resource_path,
		SPAWNER_PATH,
	) as Node2D
	player.set_meta(
		NetwPersistenceEngine.META_DATABASE,
		db,
	)
	await get_tree().process_frame
	return player


func test_server_read_snapshot_persists_live_value() -> void:
	var server_player := await _spawn_player()

	# The server holds the authoritative position and its engine reads it live.
	server_player.position = Vector2(10, 20)
	var engine: NetwPersistenceEngine = NetwEntity.of(server_player).persistence
	var api := harness.server().api
	await api.persist_flush(api.entity_of(server_player))

	var raw: Dictionary = await NetwDatabase.settled_value(
		db.backend.find_by_id(&"security", engine._record_id()),
		{ },
	)
	assert_that(raw.get(&"position")).is_equal(Vector2(10, 20))


func test_lint_predicate_distinguishes_governed_from_clean() -> void:
	var player := await _spawn_player()
	var entity := NetwEntity.of(player)

	# A governed property (position, driven by StateSync) trips the lint
	# predicate; a property no synchronizer drives (rotation) does not.
	assert_bool(entity.governs_property(entity.property_path(player, &"position"))) \
			.is_true()
	assert_bool(entity.governs_property(entity.property_path(player, &"rotation"))) \
			.is_false()
