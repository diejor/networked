## Server-authoritative [SaveComponent] security tests.
##
## A [constant SaveComponent.SaveMode.SNAPSHOT] property has no client to server
## channel: it is [constant SceneReplicationConfig.REPLICATION_MODE_NEVER] on the
## wire and the server reads its authoritative value from the live scene. A
## [constant SaveComponent.SaveMode.CLIENT] property is the declared, auditable
## client-authoritative-save case and round-trips client to server.
class_name TestSaveSecurity
extends NetwTestSuite

const SPAWNER_PATH := "SecurityPlayer/MultiplayerEntity"

var harness: NetwTestHarness
var client0: MultiplayerTree
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	db = auto_free(NetwDatabase.new())
	db.backend = NetwDatabaseBackend.Dict.new()

	var player_path := NetwPathNamespace.next_path("player", "SecurityPlayer")
	var level_path := NetwPathNamespace.next_path("level", "SecurityLevel")

	# position: SNAPSHOT, ALSO governed by StateSync (the real authority).
	# rotation: CLIENT, a clean save-only client-owned column.
	player_builder = PlayerBuilder.new("SecurityPlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_save(db, &"security") \
			.with_save_property(&"position") \
			.with_save_property(&"rotation", SaveComponent.SaveMode.CLIENT) \
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
	return await harness.join_player(
		client0,
		level_builder.resource_path,
		SPAWNER_PATH,
	) as Node2D


# Replication mode of the virtual leaf [param leaf] in [param cfg], or -1.
func _mode_of(cfg: SceneReplicationConfig, leaf: StringName) -> int:
	for path: NodePath in cfg.get_properties():
		var sub := path.get_subname_count()
		if sub > 0 and StringName(path.get_subname(sub - 1)) == leaf:
			return cfg.property_get_replication_mode(path)
	return -1


func test_snapshot_prop_is_never_on_the_wire() -> void:
	var player := await _spawn_player()
	var save: SaveComponent = player.get_node("%SaveComponent")

	# SNAPSHOT has no client->server channel; CLIENT keeps its ON_CHANGE wire.
	assert_int(_mode_of(save.replication_config, &"position")) \
			.is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)
	assert_int(_mode_of(save.replication_config, &"rotation")) \
			.is_equal(SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


func test_forged_snapshot_push_is_ignored_client_push_is_applied() -> void:
	var server_player := await _spawn_player()
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D

	# The server holds the authoritative position and snapshots it.
	server_player.position = Vector2(10, 20)
	var server_save: SaveComponent = server_player.get_node("%SaveComponent")
	server_save._snapshot_flush([&"position"])
	assert_vector(server_save.record.get_value(&"position")) \
			.is_equal(Vector2(10, 20))

	# A malicious client (its own SaveComponent's authority, so it passes the
	# ownership guard) forges a SNAPSHOT column and a legitimate CLIENT column.
	var client_save: SaveComponent = client_player.get_node("%SaveComponent")
	var forged := var_to_bytes(
		{
			&"position": Vector2(999, 999),
			&"rotation": 3.0,
		},
	)
	client_save._request_push.rpc_id(1, forged, false)

	# CLIENT key lands (proves the RPC arrived); SNAPSHOT key is dropped.
	@warning_ignore("redundant_await")
	await assert_func(server_save.record, "get_value", [&"rotation"]) \
			.wait_until(1000).is_equal(3.0)
	assert_vector(server_save.record.get_value(&"position")) \
			.is_equal(Vector2(10, 20))


func test_client_prop_round_trips_through_push_to() -> void:
	var server_player := await _spawn_player()
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D

	client_player.rotation = 1.25
	var client_save: SaveComponent = client_player.get_node("%SaveComponent")
	client_save.push_to(MultiplayerPeer.TARGET_PEER_SERVER)

	var server_save: SaveComponent = server_player.get_node("%SaveComponent")
	@warning_ignore("redundant_await")
	await assert_func(server_save.record, "get_value", [&"rotation"]) \
			.wait_until(1000).is_equal(1.25)


func test_lint_predicate_distinguishes_governed_from_clean() -> void:
	var player := await _spawn_player()
	var save: SaveComponent = player.get_node("%SaveComponent")
	var entity := NetwEntity.of(player)

	# A governed property (position, driven by StateSync) trips the lint
	# predicate; a clean client-owned property (rotation) does not.
	assert_bool(entity.governs_property(save.get_real_path(&"position"), save)) \
			.is_true()
	assert_bool(entity.governs_property(save.get_real_path(&"rotation"), save)) \
			.is_false()


func _model(modes: Dictionary, intervals: Dictionary) -> SaveComponent._SaveModel:
	var props: Array[StringName] = []
	props.assign(modes.keys())
	return SaveComponent._SaveModel.new(modes, intervals, props)


func test_save_model_due_honors_per_prop_interval() -> void:
	var model := _model(
		{ &"fast": SaveComponent.SaveMode.SNAPSHOT },
		{ &"fast": 1.0 },
	)
	assert_array(model.due(0.5, 5.0)).is_empty()
	assert_array(model.due(0.6, 5.0)).contains_exactly([&"fast"])


func test_save_model_due_inherits_fallback_interval() -> void:
	var model := _model(
		{ &"inherit": SaveComponent.SaveMode.SNAPSHOT },
		{ &"inherit": 0.0 },
	)
	assert_array(model.due(3.0, 5.0)).is_empty()
	assert_array(model.due(2.5, 5.0)).contains_exactly([&"inherit"])


func test_save_model_due_excludes_client_props() -> void:
	var model := _model(
		{ &"owned": SaveComponent.SaveMode.CLIENT },
		{ &"owned": 0.0 },
	)
	assert_array(model.due(100.0, 1.0)).is_empty()
	assert_array(model.client_props()).contains_exactly([&"owned"])
