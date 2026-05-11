## Integration tests for NetwContext.
##
## Exercises the full game-facing API across real in-process multiplayer peers:
## player queries, wait_for_players, suspend/resume, pause/unpause, kick,
## countdown, and the readiness gate — all wired through the LocalLoopback
## transport so RPCs actually travel between server and client nodes.
##
## Each test gets a fresh harness (before_test / after_test).
## Two clients are connected and two player nodes spawned before every test.
class_name TestNetwContext
extends NetworkedTestSuite

const TEST_LEVEL_SCENE    := preload("res://tests/helpers/TestLevel.tscn")
const TEST_PLAYER_SCENE   := preload("res://tests/helpers/TestPlayerMinimal.tscn")

var harness: NetworkTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree

var server_ctx:  NetwContext
var client0_ctx: NetwContext
var client1_ctx: NetwContext

## player0 lives in the server scene; authority belongs to client0.
var player0: Node
## player1 lives in the server scene; authority belongs to client1.
var player1: Node


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	harness._get_scene_manager(harness.get_server()) \
		.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	player0 = harness.spawn_player(client0, TEST_PLAYER_SCENE)
	player1 = harness.spawn_player(client1, TEST_PLAYER_SCENE)

	# Block until both players appear on each client so RPC paths are warm.
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")
	await harness.wait_for_client_player_spawn(client1, &"TestLevel")

	server_ctx = harness.get_server_scene().get_context()

	var c0_scene := await harness.wait_for_client_scene_spawn(client0, &"TestLevel")
	var c1_scene := await harness.wait_for_client_scene_spawn(client1, &"TestLevel")
	client0_ctx = c0_scene.get_context()
	client1_ctx = c1_scene.get_context()


func after_test() -> void:
	# Safety: ensure the tree is never left paused, which would deadlock teardown.
	get_tree().paused = false

	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


# ---------------------------------------------------------------------------
# Player queries
# ---------------------------------------------------------------------------

func test_get_players_returns_all_spawned() -> void:
	var players := server_ctx.scene.get_players()
	assert_that(players.size()).is_equal(2)
	assert_that(players.has(player0)).is_true()
	assert_that(players.has(player1)).is_true()


func test_get_player_by_peer_id_returns_correct_player() -> void:
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()

	assert_that(server_ctx.scene.get_player_by_peer_id(peer0_id)).is_equal(player0)
	assert_that(server_ctx.scene.get_player_by_peer_id(peer1_id)).is_equal(player1)


# ---------------------------------------------------------------------------
# wait_for_players
# ---------------------------------------------------------------------------

func test_wait_for_players_returns_immediately_when_count_already_met() -> void:
	# Both players are already present; wait_for_players(2) should never
	# suspend — the while-loop exits immediately and the coroutine runs to
	# completion before yielding.
	var results := { "completed": false }
	(func():
		await server_ctx.scene.wait_for_players(2)
		results.completed = true).call()

	# No frame advance needed: the coroutine was never suspended.
	assert_that(results.completed).is_true()


func test_wait_for_players_suspends_until_player_enters() -> void:
	# Separate fresh scene with zero players so the loop must actually await.
	var h: NetworkTestHarness = auto_free(NetworkTestHarness.new())
	add_child(h)
	await h.setup(NetworkedTestSuite.create_scene_manager)
	h._get_scene_manager(h.get_server()).add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	var c: MultiplayerTree = await h.add_client()
	var ctx: NetwContext = h.get_server_scene().get_context()

	var results := { "resolved": false }
	(func():
		await ctx.scene.wait_for_players(1)
		results.resolved = true).call()

	await get_tree().process_frame
	assert_that(results.resolved).is_false()

	h.spawn_player(c, TEST_PLAYER_SCENE)
	await wait_until(func(): return results.resolved)
	assert_that(results.resolved).is_true()


# ---------------------------------------------------------------------------
# Suspend / resume  (signal-only broadcast, does not touch get_tree().paused)
# ---------------------------------------------------------------------------

func test_suspend_signal_reaches_client_context() -> void:
	var results := { "received_reason": "" }
	client0_ctx.scene.suspended.connect(func(r): results.received_reason = r)

	server_ctx.scene.suspend("loading")

	await wait_until(func(): return not results.received_reason.is_empty())
	assert_that(results.received_reason).is_equal("loading")


func test_request_suspend_notifies_server() -> void:
	var results := { "requester_id": -1, "received_reason": "" }
	server_ctx.scene.suspend_requested.connect(func(pid, r):
		results.requester_id = pid
		results.received_reason = r)

	client0_ctx.scene.request_suspend("brb")

	await wait_until(func(): return results.requester_id != -1)
	assert_that(results.requester_id).is_equal(client0.multiplayer_peer.get_unique_id())
	assert_that(results.received_reason).is_equal("brb")


func test_resume_signal_reaches_client_context() -> void:
	# Suspend first, then resume, and verify the resume reaches client1.
	server_ctx.scene.suspend("")
	var results := { "resume_received": false }
	client1_ctx.scene.resumed.connect(func(): results.resume_received = true)

	server_ctx.scene.resume()

	await wait_until(func(): return results.resume_received)
	assert_that(results.resume_received).is_true()


# ---------------------------------------------------------------------------
# Pause / unpause  (hard, get_tree().paused, broadcast via call_local RPC)
#
# Client-side delivery cannot be awaited while the tree is paused because the
# loopback backend polls in _process, which is gated by the pause.  We verify
# the server-side half (synchronous via call_local) and the round-trip via
# unpause below.
# ---------------------------------------------------------------------------

func test_pause_sets_tree_paused_on_server_synchronously() -> void:
	var results := { "paused_reason": "" }
	server_ctx.tree.tree_paused.connect(func(r): results.paused_reason = r)

	server_ctx.tree.pause("waiting")

	# call_local fires the method on the server in the same call frame.
	assert_that(results.paused_reason).is_equal("waiting")
	assert_that(get_tree().paused).is_true()

	server_ctx.tree.unpause()  # restore before leaving test


func test_unpause_clears_tree_paused_and_emits_signal() -> void:
	server_ctx.tree.pause("")
	assert_that(get_tree().paused).is_true()

	var results := { "unpaused_fired": false }
	server_ctx.tree.tree_unpaused.connect(func(): results.unpaused_fired = true)
	server_ctx.tree.unpause()

	assert_that(get_tree().paused).is_false()
	assert_that(results.unpaused_fired).is_true()


# ---------------------------------------------------------------------------
# Kick
# ---------------------------------------------------------------------------

func test_kick_disconnects_the_peer() -> void:
	var server := harness.get_server()
	var peer0_id := client0.multiplayer_peer.get_unique_id()

	var results := { "disconnected_id": -1 }
	server.peer_disconnected.connect(func(id): results.disconnected_id = id, CONNECT_ONE_SHOT)

	server_ctx.tree.kick(peer0_id)

	await wait_until(func(): return results.disconnected_id != -1)
	assert_that(results.disconnected_id).is_equal(peer0_id)


func test_request_kick_notifies_server() -> void:
	var peer1_id := client1.multiplayer_peer.get_unique_id()

	var results := { "received_requester": -1, "received_target": -1 }
	server_ctx.tree.kick_requested.connect(func(requester, target, _r):
		results.received_requester = requester
		results.received_target    = target)

	# client0 asks the server to kick client1.
	client0_ctx.tree.request_kick(peer1_id, "griefing")

	await wait_until(func(): return results.received_requester != -1)
	assert_that(results.received_requester).is_equal(client0.multiplayer_peer.get_unique_id())
	assert_that(results.received_target).is_equal(peer1_id)


# ---------------------------------------------------------------------------
# Countdown
# ---------------------------------------------------------------------------

func test_cancel_countdown_fires_immediately_without_waiting() -> void:
	var results := { "cancelled": false }
	server_ctx.scene.countdown_cancelled.connect(func(): results.cancelled = true)

	var cd := server_ctx.scene.start_countdown(30)
	assert_that(cd.is_running()).is_true()

	server_ctx.scene.cancel_countdown()

	# All signal emissions are synchronous — no frame advance needed.
	assert_that(results.cancelled).is_true()
	assert_that(cd.is_running()).is_false()


func test_countdown_started_signal_reaches_client() -> void:
	var results := { "client_seconds": -1 }
	client0_ctx.scene.countdown_started.connect(func(s): results.client_seconds = s)

	server_ctx.scene.start_countdown(10)
	await wait_until(func(): return results.client_seconds != -1)

	assert_that(results.client_seconds).is_equal(10)
	server_ctx.scene.cancel_countdown()


## NOTE: this test takes ~1 second (one real-time timer tick).
func test_countdown_tick_and_finished_fire_in_order() -> void:
	var events: Array[String] = []
	server_ctx.scene.countdown_tick.connect(func(s): events.append("tick:%d" % s))
	server_ctx.scene.countdown_finished.connect(func(): events.append("finished"))

	server_ctx.scene.start_countdown(1)

	await wait_until(func(): return "finished" in events, 3.0)

	# start_countdown(1): one tick at 0 seconds, then finished.
	assert_that(events.size()).is_equal(2)
	assert_that(events[0]).is_equal("tick:0")
	assert_that(events[1]).is_equal("finished")


# ---------------------------------------------------------------------------
# Readiness gate
# ---------------------------------------------------------------------------

func test_readiness_gate_pre_populated_with_current_players() -> void:
	# Server scene has tracked_nodes for both players; gate should reflect this.
	var gate := server_ctx.scene.create_readiness_gate()
	var peer0_id := client0.multiplayer_peer.get_unique_id()
	var peer1_id := client1.multiplayer_peer.get_unique_id()

	assert_that(gate._readiness.has(peer0_id)).is_true()
	assert_that(gate._readiness.has(peer1_id)).is_true()
	assert_that(gate.is_peer_ready(peer0_id)).is_false()
	assert_that(gate.is_peer_ready(peer1_id)).is_false()


func test_set_ready_propagates_to_server_gate_via_rpc() -> void:
	var server_gate := server_ctx.scene.create_readiness_gate()
	var c0_gate     := client0_ctx.scene.create_readiness_gate()

	var results := { "changed_peer": -1, "changed_state": false }
	server_gate.player_ready_changed.connect(func(pid, r):
		results.changed_peer  = pid
		results.changed_state = r)

	c0_gate.set_ready(true)

	await wait_until(func(): return results.changed_peer != -1)
	assert_that(results.changed_peer).is_equal(client0.multiplayer_peer.get_unique_id())
	assert_that(results.changed_state).is_true()


func test_all_ready_fires_when_every_player_is_ready() -> void:
	var server_gate := server_ctx.scene.create_readiness_gate()
	var c0_gate     := client0_ctx.scene.create_readiness_gate()
	var c1_gate     := client1_ctx.scene.create_readiness_gate()

	var results := { "all_ready_fired": false }
	server_gate.all_ready.connect(func(): results.all_ready_fired = true)

	c0_gate.set_ready(true)
	c1_gate.set_ready(true)

	await wait_until(func(): return results.all_ready_fired)
	assert_that(results.all_ready_fired).is_true()


func test_player_leave_removes_entry_from_gate() -> void:
	var server_gate := server_ctx.scene.create_readiness_gate()
	var peer0_id    := client0.multiplayer_peer.get_unique_id()

	assert_that(server_gate._readiness.has(peer0_id)).is_true()

	# Untracks the player from the synchronizer, firing context._on_despawned,
	# which calls _notify_gates_player_removed for registered gates.
	harness.get_server_scene().synchronizer.untrack_node(player0)
	await get_tree().process_frame

	assert_that(server_gate._readiness.has(peer0_id)).is_false()
