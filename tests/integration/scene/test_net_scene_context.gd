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

## player0 lives in the server scene; authority belongs to client0.
var player0: Node
## player1 lives in the server scene; authority belongs to client1.
var player1: Node


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D)
	player_builder.pack()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	player0 = harness.spawn_player(client0, player_builder.packed)
	player1 = harness.spawn_player(client1, player_builder.packed)

	# Block until both players appear on each client so RPC paths are warm.
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
	# Safety: ensure the tree is not left paused.
	get_tree().paused = false
	await super.after_test()


func test_scene_reports_current_players() -> void:
	var players := server_ctx.scene.get_players()
	assert_that(players.size()).is_equal(2)
	assert_that(players.has(player0)).is_true()
	assert_that(players.has(player1)).is_true()

	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()

	assert_that(server_ctx.scene.get_player_by_peer_id(peer0_id)).is_equal(
		player0,
	)
	assert_that(server_ctx.scene.get_player_by_peer_id(peer1_id)).is_equal(
		player1,
	)

	# wait_for_players(2) should never suspend if count is already met.
	var results := { "completed": false }
	(func():
		await server_ctx.scene.wait_for_players(2)
		results.completed = true
	).call()

	assert_that(results.completed).is_true()


func test_wait_for_players_suspends_until_player_enters() -> void:
	var h := make_unmanaged_harness()
	await h.setup(NetwTestSuite.create_scene_manager)
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
	await wait_until(func(): return results.resolved)
	assert_that(results.resolved).is_true()
	await h.teardown()


func test_suspend_and_resume_signals_reach_client_contexts() -> void:
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


func test_request_suspend_notifies_server() -> void:
	monitor_signals(server_ctx.scene, false)

	client0_ctx.scene.request_suspend("brb")

	@warning_ignore("redundant_await")
	await assert_signal(server_ctx.scene) \
			.wait_until(1000) \
			.is_emitted(
				"suspend_requested",
				[client0.multiplayer_peer.get_unique_id(), "brb"],
			)


func test_pause_sets_tree_paused_on_server_synchronously() -> void:
	var results := { "paused_reason": "" }
	server_ctx.tree.tree_paused.connect(func(r): results.paused_reason = r)

	server_ctx.tree.pause("waiting")

	# call_local fires the method on the server in the same call frame.
	assert_that(results.paused_reason).is_equal("waiting")
	assert_that(get_tree().paused).is_true()

	server_ctx.tree.unpause() # restore before leaving test


func test_unpause_clears_tree_paused_and_emits_signal() -> void:
	server_ctx.tree.pause("")
	assert_that(get_tree().paused).is_true()

	var results := { "unpaused_fired": false }
	server_ctx.tree.tree_unpaused.connect(func(): results.unpaused_fired = true)
	server_ctx.tree.unpause()

	assert_that(get_tree().paused).is_false()
	assert_that(results.unpaused_fired).is_true()


func test_kick_disconnects_the_peer() -> void:
	var server := harness.server()
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	monitor_signals(server, false)

	server_ctx.tree.kick(peer0_id)

	@warning_ignore("redundant_await")
	await assert_signal(server) \
			.wait_until(1000) \
			.is_emitted("peer_disconnected", [peer0_id])


func test_request_kick_notifies_server() -> void:
	var peer1_id := client1.multiplayer_peer.get_unique_id()
	monitor_signals(server_ctx.tree, false)

	# client0 asks the server to kick client1.
	client0_ctx.tree.request_kick(peer1_id, "griefing")

	@warning_ignore("redundant_await")
	await assert_signal(server_ctx.tree) \
			.wait_until(1000) \
			.is_emitted(
				"kick_requested",
				[client0.multiplayer_peer.get_unique_id(), peer1_id, "griefing"],
			)


func test_cancel_countdown_fires_immediately_without_waiting() -> void:
	var results := { "cancelled": false }
	server_ctx.scene.countdown_cancelled.connect(
		func(): results.cancelled = true
	)

	var cd := server_ctx.scene.start_countdown(30)
	assert_that(cd.is_running()).is_true()

	server_ctx.scene.cancel_countdown()

	assert_that(results.cancelled).is_true()
	assert_that(cd.is_running()).is_false()


func test_countdown_started_signal_reaches_client() -> void:
	monitor_signals(client0_ctx.scene, false)

	server_ctx.scene.start_countdown(10)

	@warning_ignore("redundant_await")
	await assert_signal(client0_ctx.scene) \
			.wait_until(1000) \
			.is_emitted("countdown_started", [10])

	server_ctx.scene.cancel_countdown()


func test_countdown_tick_and_finished_fire_in_order() -> void:
	var events: Array[String] = []
	server_ctx.scene.countdown_tick.connect(func(s): events.append("tick:%d" % s))
	server_ctx.scene.countdown_finished.connect(func(): events.append("finished"))

	server_ctx.scene.start_countdown(1, 0.03)

	await wait_until(func(): return "finished" in events, 1.0)

	# start_countdown(1): one tick at 0 seconds, then finished.
	assert_that(events.size()).is_equal(2)
	assert_that(events[0]).is_equal("tick:0")
	assert_that(events[1]).is_equal("finished")


func test_readiness_gate_tracks_players_and_ready_state() -> void:
	var server_gate := server_ctx.scene.create_readiness_gate()
	var c0_gate := client0_ctx.scene.create_readiness_gate()
	var c1_gate := client1_ctx.scene.create_readiness_gate()
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()
	monitor_signals(server_gate, false)

	assert_that(server_gate._readiness.has(peer0_id)).is_true()
	assert_that(server_gate._readiness.has(peer1_id)).is_true()
	assert_that(server_gate.is_peer_ready(peer0_id)).is_false()
	assert_that(server_gate.is_peer_ready(peer1_id)).is_false()

	c0_gate.set_ready(true)

	@warning_ignore("redundant_await")
	await assert_signal(server_gate) \
			.wait_until(1000) \
			.is_emitted(
				"player_ready_changed",
				[client0.multiplayer_peer.get_unique_id(), true],
			)

	c1_gate.set_ready(true)

	@warning_ignore("redundant_await")
	await assert_signal(server_gate) \
			.wait_until(1000) \
			.is_emitted("all_ready")


func test_player_leave_removes_entry_from_gate() -> void:
	var server_gate := server_ctx.scene.create_readiness_gate()
	var peer0_id := client0.multiplayer_peer.get_unique_id()

	assert_that(server_gate._readiness.has(peer0_id)).is_true()

	harness.scene_on_server().untrack_node(player0)
	await get_tree().process_frame

	assert_that(server_gate._readiness.has(peer0_id)).is_false()
