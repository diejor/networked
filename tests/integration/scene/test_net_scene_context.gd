## Integration tests for [NetwContext] and [NetwSceneContext].
class_name TestNetwContext
extends NetwTestSuite

var player_builder: PlayerBuilder
var level_builder: LevelBuilder

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree

var server_ctx: NetwContext
var client0_ctx: NetwContext
var client1_ctx: NetwContext

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

	server_ctx = harness.scene_on_server().get_context()

	var c0_scene := await harness.wait_for_scene(
		client0,
		level_builder.scene_name,
	)
	var c1_scene := await harness.wait_for_scene(
		client1,
		level_builder.scene_name,
	)
	client0_ctx = c0_scene.get_context()
	client1_ctx = c1_scene.get_context()


func after_test() -> void:
	get_tree().paused = false
	await super.after_test()


func test_scene_membership_flow() -> void:
	_assert_current_players()
	_assert_wait_for_players_suspends_until_player_enters()
	await _assert_bodyless_membership_flow()
	await _assert_move_participants_flow()


func test_scene_rpc_controls_flow() -> void:
	monitor_signals(client0_ctx.scene, false)

	server_ctx.scene.suspend("loading")

	@warning_ignore("redundant_await")
	await assert_signal(client0_ctx.scene) \
			.wait_until(1000) \
			.is_emitted("suspended", ["loading"])

	monitor_signals(client1_ctx.scene, false)

	server_ctx.scene.resume()

	@warning_ignore("redundant_await")
	await assert_signal(client1_ctx.scene) \
			.wait_until(1000) \
			.is_emitted("resumed")

	monitor_signals(server_ctx.scene, false)

	client0_ctx.scene.request_suspend("brb")

	@warning_ignore("redundant_await")
	await assert_signal(server_ctx.scene) \
			.wait_until(1000) \
			.is_emitted(
				"suspend_requested",
				[client0.multiplayer_peer.get_unique_id(), "brb"],
			)

	var pause_result := { "paused_reason": "" }
	server_ctx.tree.tree_paused.connect(func(r): pause_result.paused_reason = r)

	server_ctx.tree.pause("waiting")

	assert_that(pause_result.paused_reason).is_equal("waiting")
	assert_that(get_tree().paused).is_true()

	var unpaused := { "fired": false }
	server_ctx.tree.tree_unpaused.connect(func(): unpaused.fired = true)
	server_ctx.tree.unpause()

	assert_that(get_tree().paused).is_false()
	assert_that(unpaused.fired).is_true()

	var peer1_id := client1.multiplayer_peer.get_unique_id()
	monitor_signals(server_ctx.tree, false)

	client0_ctx.tree.request_kick(peer1_id, "griefing")

	@warning_ignore("redundant_await")
	await assert_signal(server_ctx.tree) \
			.wait_until(1000) \
			.is_emitted(
				"kick_requested",
				[client0.multiplayer_peer.get_unique_id(), peer1_id, "griefing"],
			)

	var server := harness.server()
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	monitor_signals(server, false)

	server_ctx.tree.kick(peer0_id)

	@warning_ignore("redundant_await")
	await assert_signal(server) \
			.wait_until(1000) \
			.is_emitted("peer_disconnected", [peer0_id])


func test_scene_countdown_flow() -> void:
	var cancelled := { "fired": false }
	server_ctx.scene.countdown_cancelled.connect(
		func(): cancelled.fired = true
	)

	var cd := server_ctx.scene.start_countdown(30)
	assert_that(cd.is_running()).is_true()

	server_ctx.scene.cancel_countdown()

	assert_that(cancelled.fired).is_true()
	assert_that(cd.is_running()).is_false()

	monitor_signals(client0_ctx.scene, false)

	server_ctx.scene.start_countdown(10)

	@warning_ignore("redundant_await")
	await assert_signal(client0_ctx.scene) \
			.wait_until(1000) \
			.is_emitted("countdown_started", [10])

	server_ctx.scene.cancel_countdown()

	var events: Array[String] = []
	server_ctx.scene.countdown_tick.connect(func(s): events.append("tick:%d" % s))
	server_ctx.scene.countdown_finished.connect(func(): events.append("finished"))

	monitor_signals(server_ctx.scene, false)
	server_ctx.scene.start_countdown(1, 0.03)

	@warning_ignore("redundant_await")
	await assert_signal(server_ctx.scene) \
			.wait_until(1000) \
			.is_emitted("countdown_finished")

	assert_that(events.size()).is_equal(2)
	assert_that(events[0]).is_equal("tick:0")
	assert_that(events[1]).is_equal("finished")


func test_readiness_gate_flow() -> void:
	var server_gate := server_ctx.scene.create_readiness_gate()
	var c0_gate := client0_ctx.scene.create_readiness_gate()
	var c1_gate := client1_ctx.scene.create_readiness_gate()
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()
	var participant0 := server_ctx.tree.participant(peer0_id)
	var participant1 := server_ctx.tree.participant(peer1_id)
	monitor_signals(server_gate, false)

	assert_that(server_gate.tracks(participant0)).is_true()
	assert_that(server_gate.tracks(participant1)).is_true()
	assert_that(server_gate.is_ready(participant0)).is_false()
	assert_that(server_gate.is_ready(participant1)).is_false()

	c0_gate.set_ready(true)

	@warning_ignore("redundant_await")
	await assert_signal(server_gate) \
			.wait_until(1000) \
			.is_emitted(
				"participant_ready_changed",
				[participant0, true],
			)

	c1_gate.set_ready(true)

	@warning_ignore("redundant_await")
	await assert_signal(server_gate) \
			.wait_until(1000) \
			.is_emitted("all_ready")

	server_ctx.scene.release(participant0)
	await get_tree().process_frame

	assert_that(server_gate.tracks(participant0)).is_false()

	await _assert_bodyless_readiness_gate()


func _assert_current_players() -> void:
	var players := server_ctx.scene.players
	assert_that(players.size()).is_equal(2)
	assert_that(players.has(NetwEntity.of(player0))).is_true()
	assert_that(players.has(NetwEntity.of(player1))).is_true()

	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()

	assert_that(server_ctx.scene.get_player_by_peer_id(peer0_id)).is_equal(
		NetwEntity.of(player0),
	)
	assert_that(server_ctx.scene.get_player_by_peer_id(peer1_id)).is_equal(
		NetwEntity.of(player1),
	)

	assert_that(NetwEntity.of(player0).participant) \
			.is_equal(server_ctx.tree.participant(peer0_id))
	assert_that(NetwEntity.of(player1).participant) \
			.is_equal(server_ctx.tree.participant(peer1_id))

	var results := { "completed": false }
	(func():
		await server_ctx.scene.wait_for_players(2)
		results.completed = true
	).call()

	assert_that(results.completed).is_true()


func _assert_wait_for_players_suspends_until_player_enters() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	var c: MultiplayerTree = await h.add_client()
	var ctx: NetwContext = h.scene_on_server().get_context()

	var results := { "resolved": false }
	(func():
		await ctx.scene.wait_for_players(1)
		results.resolved = true
	).call()

	await get_tree().process_frame
	assert_that(results.resolved).is_false()

	h.spawn_player(c, player_builder.packed)
	await h.wait_for_player(h.server(), level_builder.scene_name)
	assert_that(results.resolved).is_true()
	await h.teardown()


func _assert_bodyless_membership_flow() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	var c := await h.add_client()
	var ctx := h.scene_on_server().get_context()
	var peer_id := c.multiplayer_peer.get_unique_id()
	var participant := ctx.tree.participant(peer_id)
	var events: Array[StringName] = []
	var entered := { "fired": false }
	var waited := { "resolved": false }

	c.local_scene_changed.connect(
		func(_from: NetwScene, to: NetwScene) -> void:
			events.append(to.scene_name if to else &"")
	)
	ctx.scene.participant_entered.connect(
		func(p: NetwParticipant) -> void:
			entered.fired = p == participant
	)
	(func():
		await ctx.scene.wait_for_participants(1)
		waited.resolved = true
	).call()

	await get_tree().process_frame
	assert_that(waited.resolved).is_false()

	ctx.scene.admit(participant)
	await _wait_for_event_count(events, 1)

	assert_that(entered.fired).is_true()
	assert_that(waited.resolved).is_true()
	assert_that(ctx.scene.peers.has(peer_id)).is_true()
	assert_that(participant.current_scene.unwrap()).is_equal(ctx.scene.unwrap())
	assert_that(events[0]).is_equal(level_builder.scene_name)
	assert_that(c.local_participant.current_scene.scene_name) \
			.is_equal(level_builder.scene_name)

	ctx.scene.release(participant)
	await _wait_for_event_count(events, 2)

	assert_that(events[1]).is_equal(&"")
	assert_that(c.local_participant.current_scene).is_null()
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
	var participant := h.server().get_participant(
		c.multiplayer_peer.get_unique_id(),
	)
	var source := h.scene_on_server(source_builder.scene_name).get_context().scene
	var dest := h.scene_on_server(dest_builder.scene_name).get_context().scene
	var events: Array[String] = []

	source.participant_left.connect(
		func(_p: NetwParticipant) -> void: events.append("left")
	)
	dest.participant_entered.connect(
		func(_p: NetwParticipant) -> void: events.append("entered")
	)
	source.admit(participant)

	var batch := dest.move_participants([participant])
	batch.participant_arrived.connect(
		func(_p: NetwParticipant) -> void: events.append("arrived")
	)
	await batch.completed

	assert_that(events.has("left")).is_true()
	assert_that(events.has("entered")).is_true()
	assert_that(events.has("arrived")).is_true()
	assert_that(source.peers.has(participant.peer_id)).is_false()
	assert_that(dest.peers.has(participant.peer_id)).is_true()
	assert_that(participant.current_scene.unwrap()).is_equal(dest.unwrap())
	await h.teardown()


func _assert_bodyless_readiness_gate() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	var c := await h.add_client()
	var server_scene := h.scene_on_server()
	var ctx := server_scene.get_context()
	var peer_id := c.multiplayer_peer.get_unique_id()
	var participant := ctx.tree.participant(peer_id)

	ctx.scene.admit(participant)
	var client_scene := await h.wait_for_scene(c, level_builder.scene_name)
	var server_gate := ctx.scene.create_readiness_gate()
	var client_gate := client_scene.get_context().scene.create_readiness_gate()

	assert_that(server_gate.tracks(participant)).is_true()
	assert_that(server_gate.is_ready(participant)).is_false()
	monitor_signals(server_gate, false)

	client_gate.set_ready(true)

	@warning_ignore("redundant_await")
	await assert_signal(server_gate) \
			.wait_until(1000) \
			.is_emitted("all_ready")
	await h.teardown()


func _wait_for_event_count(events: Array[StringName], count: int) -> void:
	for i in 60:
		if events.size() >= count:
			return
		await get_tree().process_frame
	assert_int(events.size()).is_greater_equal(count)
