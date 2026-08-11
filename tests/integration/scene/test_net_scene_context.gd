## Integration tests for the scene handle's session helpers.
class_name TestNetSceneContext
extends NetwTestSuite

var player_builder: PlayerBuilder
var level_builder: LevelBuilder

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree

var server_ctx: NetwSceneHandle
var client0_ctx: NetwSceneHandle
var client1_ctx: NetwSceneHandle

## player0 lives in the server scene. Authority belongs to client0.
var player0: Node
## player1 lives in the server scene. Authority belongs to client1.
var player1: Node


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D).with_multiplayer_entity()
	player_builder.pack()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	player0 = harness.spawn_player(client0, player_builder.packed)
	player1 = harness.spawn_player(client1, player_builder.packed)

	await harness.wait_for_player(client0, level_builder.scene_name)
	await harness.wait_for_player(client1, level_builder.scene_name)

	server_ctx = harness.scene_on_server()

	client0_ctx = await harness.wait_for_scene(
		client0,
		level_builder.scene_name,
	)
	client1_ctx = await harness.wait_for_scene(
		client1,
		level_builder.scene_name,
	)


func after_test() -> void:
	get_tree().paused = false
	await super.after_test()


func test_scene_membership_flow() -> void:
	_assert_current_players()
	_assert_wait_for_players_suspends_until_player_enters()
	await _assert_bodyless_membership_flow()
	await _assert_move_participants_flow()


func test_scene_rpc_controls_flow() -> void:
	var server_tree := harness.server().api

	var pause_result := { "paused_reason": "" }
	server_tree.tree_paused.connect(func(r): pause_result.paused_reason = r)

	server_tree.session.pause("waiting")

	assert_that(pause_result.paused_reason).is_equal("waiting")
	assert_that(get_tree().paused).is_true()

	var unpaused := { "fired": false }
	server_tree.tree_unpaused.connect(func(): unpaused.fired = true)
	server_tree.session.unpause()

	assert_that(get_tree().paused).is_false()
	assert_that(unpaused.fired).is_true()

	var peer1_id := client1.multiplayer_peer.get_unique_id()
	monitor_signals(server_tree, false)

	client0.api.peer_request_kick(peer1_id, "griefing")

	@warning_ignore("redundant_await")
	await assert_signal(server_tree) \
			.wait_until(1000) \
			.is_emitted(
				"kick_requested",
				[client0.multiplayer_peer.get_unique_id(), peer1_id, "griefing"],
			)

	var server := harness.server()
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	monitor_signals(server, false)

	server_tree.peer_kick(peer0_id)

	@warning_ignore("redundant_await")
	await assert_signal(server.api) \
			.wait_until(1000) \
			.is_emitted("peer_disconnected", [peer0_id])


func _assert_current_players() -> void:
	var server_tree := harness.server().api
	var players := server_ctx.players
	assert_that(players.size()).is_equal(2)
	assert_that(players.has(NetwEntity.of(player0))).is_true()
	assert_that(players.has(NetwEntity.of(player1))).is_true()

	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()

	assert_that(server_ctx.player_by_peer(peer0_id)).is_equal(
		NetwEntity.of(player0),
	)
	assert_that(server_ctx.player_by_peer(peer1_id)).is_equal(
		NetwEntity.of(player1),
	)

	assert_that(NetwEntity.of(player0).participant) \
			.is_equal(server_tree.peer_get_participant(peer0_id))
	assert_that(NetwEntity.of(player1).participant) \
			.is_equal(server_tree.peer_get_participant(peer1_id))

	assert_bool(await harness.wait_for_players(server_ctx, 2)).is_false()


func _assert_wait_for_players_suspends_until_player_enters() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	var c: MultiplayerTree = await h.add_client()
	var ctx := h.scene_on_server()

	# The scene is empty until a player is actually seated into it.
	assert_int(ctx.players.size()).is_equal(0)

	h.spawn_player(c, player_builder.packed)
	await h.wait_for_player(h.server(), level_builder.scene_name)
	assert_int(ctx.players.size()).is_equal(1)
	await h.teardown()


func _assert_bodyless_membership_flow() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	var c := await h.add_client()
	var ctx := h.scene_on_server()
	var peer_id := c.multiplayer_peer.get_unique_id()
	var participant := h.server().api.peer_get_participant(peer_id)
	var events: Array[StringName] = []
	var entered := { "fired": false }

	c.api.local_scene_changed.connect(
		func(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
			events.append(to.label if to else &"")
	)
	ctx.on_participant_entered(
		func(p: NetwParticipant) -> void:
			entered.fired = p == participant
	)
	assert_int(ctx.participants.size()).is_equal(0)

	ctx.admit(participant)
	await _wait_for_event_count(events, 1)

	assert_that(entered.fired).is_true()
	assert_that(ctx.peers.has(peer_id)).is_true()
	assert_that(participant.current_scene).is_equal(ctx)
	assert_that(events[0]).is_equal(level_builder.scene_name)
	assert_that(c.api.local_participant.current_scene.label) \
			.is_equal(level_builder.scene_name)

	ctx.release(participant)
	await _wait_for_event_count(events, 2)

	assert_that(events[1]).is_equal(&"")
	assert_that(c.api.local_participant.current_scene).is_null()
	await h.teardown()


func _assert_move_participants_flow() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	var source_builder := LevelBuilder.new("MoveSource").with_root(Node2D)
	source_builder.pack()
	var dest_builder := LevelBuilder.new("MoveDest").with_root(Node2D)
	dest_builder.pack()
	h.register_spawnable_scene(source_builder.packed)
	h.register_spawnable_scene(dest_builder.packed)
	var c := await h.add_client()
	var participant := h.server().api.peer_get_participant(
		c.multiplayer_peer.get_unique_id(),
	)
	var source := h.scene_on_server(source_builder.scene_name)
	var dest := h.scene_on_server(dest_builder.scene_name)
	var events: Array[String] = []

	source.on_participant_left(
		func(_p: NetwParticipant) -> void: events.append("left")
	)
	dest.on_participant_entered(
		func(_p: NetwParticipant) -> void: events.append("entered")
	)
	source.admit(participant)

	var batch := dest.move_participants([participant])
	batch.completed_single.connect(
		func(_peer: int, _p: NetwParticipant) -> void: events.append("arrived")
	)
	await batch.completed

	assert_that(events.has("left")).is_true()
	assert_that(events.has("entered")).is_true()
	assert_that(events.has("arrived")).is_true()
	assert_that(source.peers.has(participant.peer_id)).is_false()
	assert_that(dest.peers.has(participant.peer_id)).is_true()
	assert_that(participant.current_scene).is_equal(dest)
	await h.teardown()


func _wait_for_event_count(events: Array[StringName], count: int) -> void:
	for i in 60:
		if events.size() >= count:
			return
		await get_tree().process_frame
	assert_int(events.size()).is_greater_equal(count)
