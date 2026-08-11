class_name TestRelayRaces
extends NetwTestSuite

var harness: NetwTestHarness
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("LivenessRacePlayer") \
			.with_root(StateSyncBody) \
			.with_multiplayer_entity() \
			.with_state([&"position"])
	player_builder.pack()

	level_builder = LevelBuilder.new("LivenessRaceLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	harness = null

	player_builder = null
	level_builder = null

	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


func test_spawn_edge_race() -> void:
	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.register_initial_scene_path(level_builder.resource_path)
		sm.register_scene_path(player_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)

	var server = harness.server()
	var client = await harness.add_client("jose")

	# Verify the client carrier has drops_unknown_route incremented when
	# receiving a carrier for an unknown route
	var client_api: NetwMultiplayer = client.api

	# Send carrier on unknown route 99
	var framed = _frame_carrier(99)
	client_api._drive_carrier(0, framed, true)

	var snapshot = client_api.stats_snapshot()
	assert_that(snapshot.get("drops_unknown_route", 0)).is_equal(1)

	# Spawn player normally and ensure it converges
	var player = harness.spawn_player(client, player_builder.packed)
	var client_player = await harness.wait_for_player(client, level_builder.scene_name)
	assert_that(client_player).is_not_null()

	var client_entity = NetwEntity.of(client_player)
	var server_entity = NetwEntity.of(player)
	var client_liveness: LivenessShell = client.api._liveness
	var server_liveness: LivenessShell = server.api._liveness

	var route = server_liveness.route_of(server_entity)
	assert_that(route).is_greater(0)
	assert_that(client_liveness.route_of(client_entity)).is_equal(route)


func test_despawn_edge_race() -> void:
	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.register_initial_scene_path(level_builder.resource_path)
		sm.register_scene_path(player_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)

	var server = harness.server()
	var client = await harness.add_client("jose")

	# Spawn player
	var player = harness.spawn_player(client, player_builder.packed)
	await harness.wait_for_player(client, level_builder.scene_name)

	var me = NetwEntity.of(player)
	var route = me.route
	assert_that(route).is_greater(0)

	# Despawn player on server
	me.despawn()

	# Wait a frame for server node to queue_free and exit tree
	await NetwTestSuite.drain_frames(get_tree(), 1)

	# Assert that route is dead on server
	var server_liveness: LivenessShell = server.api._liveness
	assert_that(server_liveness.route_state(route)).is_equal(
		LivenessShell.State.DEAD,
	)

	# Step client to receive the despawn packet and update route state
	await NetwTestSuite.drain_frames(get_tree(), 5)

	# Verify route is no longer live on client
	var client_liveness: LivenessShell = client.api._liveness
	assert_that(client_liveness.route_state(route)).is_equal(
		LivenessShell.State.DEAD,
	)

	# Verify the client carrier drops frames on a dead route under drops_not_live
	var client_api: NetwMultiplayer = client.api
	var framed = _frame_carrier(route)
	client_api._drive_carrier(0, framed, true)

	var snapshot = client_api.stats_snapshot()
	assert_that(snapshot.get("drops_not_live", 0)).is_equal(1)


func test_linger_variant() -> void:
	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.register_initial_scene_path(level_builder.resource_path)
		sm.register_scene_path(player_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)

	var server = harness.server()
	var client = await harness.add_client("jose")

	# Spawn player
	var player = harness.spawn_player(client, player_builder.packed)
	await harness.wait_for_player(client, level_builder.scene_name)

	var me = NetwEntity.of(player)
	var route = me.route
	assert_that(route).is_greater(0)

	# Despawn with linger on server
	var opts = NetwEntity.DespawnOpts.new()
	opts.linger = true
	opts.linger_seconds = 0.5
	me.despawn(opts)

	# Server route should be LINGERING
	var server_liveness: LivenessShell = server.api._liveness
	assert_that(server_liveness.route_state(route)).is_equal(
		LivenessShell.State.LINGERING,
	)

	# Frame a carrier on the lingering route and send it to the server carrier
	var server_api: NetwMultiplayer = server.api
	var framed = _frame_carrier(route)
	server_api._drive_carrier(0, framed, true)

	var snapshot = server_api.stats_snapshot()
	assert_that(snapshot.get("drops_not_live", 0)).is_equal(1)

	# Step past linger window. The DEAD transition resolves at end-of-frame
	# (the reparent grace applies to every spawn-book route), so give the
	# deferred resolution one frame.
	await player.tree_exited
	await get_tree().process_frame

	# Route is now dead on server
	assert_that(server_liveness.route_state(route)).is_equal(
		LivenessShell.State.DEAD,
	)


func _frame_carrier(route: int) -> PackedByteArray:
	return NetwFrameEnvelope.pack(route, 0, NetwFrameEnvelope.Channel.SYNC, PackedByteArray())
