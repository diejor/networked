## Integration tests for [TickInterpolator] with real in-process networking.
##
## These tests use [LocalLoopbackSession] so no actual sockets are opened.
## Two things are verified that unit tests cannot cover:
## [ul]
## [li][b]Convergence[/b] — after the server player moves, the client's interpolated
##     position eventually reaches the target. Catches "remote players stay in place".[/li]
## [li][b]Display lag[/b] — immediately after a server move the client still shows
##     the old position, proving [member NetworkClock.display_offset] is producing
##     real lag through the buffer rather than reflecting the latest value directly.[/li]
## [/ul]
##
## Visual smoothness (choppiness) cannot be asserted in a headless test — it requires
## human inspection. The unit tests in [TestTickInterpolator] cover the mathematical
## guarantee (lerp midpoint at known clock state) that enables smoothness.
class_name TestTickInterpolatorNetwork
extends NetworkedTestSuite

## Replication interval for the test synchronizer.
## 2 ticks at 30 Hz — short enough that tests are fast.
const DELTA_INTERVAL := 0.05

const TICKRATE       := 30
## ceil(0.05 × 30) + 1 = 3 — minimum display_offset for smooth interpolation
## at the above replication rate.
const DISPLAY_OFFSET := 3

## How long to wait for the interpolated position to converge.
## display lag (3 / 30 ≈ 100 ms) + 1 replication cycle (50 ms) + generous slack.
const CONVERGE_WAIT  := 0.3

var _harness: NetworkTestHarness
var _client:  MultiplayerTree

var _server_player: Node2D
var _client_player:  Node2D


# ---------------------------------------------------------------------------
# Setup / teardown
# ---------------------------------------------------------------------------

func before_test() -> void:
	_harness = auto_free(NetworkTestHarness.new())
	add_child(_harness)
	# No lobby manager — we manage player nodes directly.
	await _harness.setup()

	# Server clock: assigned before the first add_client() call so the `configured`
	# signal (emitted when host() runs inside add_client) wires it up automatically.
	_add_clock(_harness.get_server())

	_client = await _harness.add_client()

	# Client clock: join() has already fired `configured`, so we trigger registration manually.
	var client_clock := _add_clock(_client)
	client_clock._on_tree_configured()

	# Build identically-named nodes under each peer's MultiplayerTree so the
	# MultiplayerSynchronizer can route updates by matching relative paths.
	_server_player = _build_server_node()
	_harness.get_server().add_child(_server_player)

	_client_player = _build_client_node()
	_client.add_child(_client_player)

	await get_tree().process_frame


func after_test() -> void:
	if is_instance_valid(_harness):
		_harness.teardown()
	await get_tree().process_frame


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

func _add_clock(tree: MultiplayerTree) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.name   = "NetworkClock"  # consistent name so RPC routing matches across peers
	clock.tickrate       = TICKRATE
	clock.display_offset = DISPLAY_OFFSET
	tree.add_child(clock)
	return clock


func _make_replication_config() -> SceneReplicationConfig:
	var cfg   := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	cfg.property_set_replication_mode(ppath, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	cfg.property_set_spawn(ppath, false)
	cfg.property_set_watch(ppath, true)
	return cfg


func _build_server_node() -> Node2D:
	var player := Node2D.new()
	player.name = "InterpTestPlayer"
	player.set_multiplayer_authority(1)  # server peer

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"  # explicit name so relative path matches on both sides
	sync.replication_config = _make_replication_config()
	sync.delta_interval = DELTA_INTERVAL
	player.add_child(sync)

	return player


func _build_client_node() -> Node2D:
	var player := Node2D.new()
	player.name = "InterpTestPlayer"  # same path → receives server synchronizer's packets
	player.set_multiplayer_authority(1)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"  # must match server's synchronizer name for replication routing
	sync.replication_config = _make_replication_config()
	sync.delta_interval = DELTA_INTERVAL
	player.add_child(sync)

	var interp := TickInterpolator.new()
	interp.name = "TickInterpolator"
	interp.property_modes = {&"position": TickInterpolator.Mode.LERP}
	player.add_child(interp)

	return player


func _wait_until_converged(node: Node2D, target: Vector2, timeout: float = 1.0) -> bool:
	var start_time := Time.get_ticks_msec()
	while node.position.distance_to(target) > 5.0:
		if Time.get_ticks_msec() - start_time > timeout * 1000:
			return false
		await get_tree().process_frame
	return true


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

func test_remote_player_converges_to_server_position() -> void:
	## After the server moves the player and enough time passes for display_offset
	## ticks plus one full replication cycle, the client's interpolated position
	## must be close to the target.
	##
	## This is the core regression guard for "remote players stay in place".

	var target := Vector2(300.0, 0.0)
	_server_player.position = target

	var ok := await _wait_until_converged(_client_player, target, CONVERGE_WAIT)
	assert_bool(ok).is_true()


func test_display_lag_delays_visual_update() -> void:
	## With display_offset > 0, TickInterpolator shows a position buffered several
	## ticks ago, not the latest raw value from the MultiplayerSynchronizer.
	##
	## Proof: immediately after the server moves to TARGET the client must still be
	## well short of TARGET.  We wait 50 ms — half the delta_interval of 100 ms —
	## so the synchronizer has either not yet sent or has sent but the display_offset
	## window has not yet elapsed.  In both cases the output must be at least 5 units
	## away from TARGET.  After CONVERGE_WAIT the client converges normally.

	const START  := Vector2(  0.0, 0.0)
	const TARGET := Vector2(300.0, 0.0)

	# Establish a stable snapshot at START.
	_server_player.position = START
	await _wait_until_converged(_client_player, START, CONVERGE_WAIT)

	# Move server to TARGET and sample after a short delay.
	_server_player.position = TARGET

	# 50 ms ≈ DELTA_INTERVAL (50 ms): the synchronizer may not have fired yet.
	# Even if it has, display_offset = 3 ticks ≈ 100 ms means the interpolated
	# output is still behind TARGET.  The distance check is robust to both cases.
	await get_tree().create_timer(0.04).timeout

	var dist_to_target := _client_player.position.distance_to(TARGET)
	assert_bool(dist_to_target > 5.0).is_true()

	# After the lag drains the client converges to TARGET.
	var ok := await _wait_until_converged(_client_player, TARGET, CONVERGE_WAIT)
	assert_bool(ok).is_true()


func test_teleport_snaps_instead_of_lerping() -> void:
	## If max_lerp_distance is set, a large jump should result in an immediate snap
	## rather than a several-frame slide across the world.
	
	var interp: TickInterpolator = _client_player.get_node("TickInterpolator")
	interp.max_lerp_distance = 100.0 # Anything over 100 units should snap.
	
	const START := Vector2(0.0, 0.0)
	const JUMP  := Vector2(1000.0, 0.0)
	
	# Stable start
	_server_player.position = START
	await _wait_until_converged(_client_player, START, CONVERGE_WAIT)
	
	# Trigger a large jump on server
	_server_player.position = JUMP
	
	# Wait for the synchronizer to fire (DELTA_INTERVAL = 50ms)
	await get_tree().create_timer(0.06).timeout
	
	# If it snaps, it will be at JUMP immediately once display_tick reaches it.
	# If it lerps, it would be somewhere like (50, 0) for several frames.
	# We wait a bit for display lag (100ms) to pass.
	await get_tree().create_timer(0.1).timeout
	
	# Should be at JUMP (or very close if we hit exactly on a frame)
	assert_vector(_client_player.position).is_equal_approx(JUMP, Vector2(1.0, 1.0))


func test_authority_handover_disables_interpolation() -> void:
	## If a client gains authority of a node (e.g. entering a vehicle), the
	## interpolator must stop overriding the position so local input works.

	const START := Vector2(0.0, 0.0)
	const CLIENT_MOVE := Vector2(50.0, 50.0)

	# Stable start as proxy
	_server_player.position = START
	await _wait_until_converged(_client_player, START, CONVERGE_WAIT)
	
	# Hand over authority to the client peer on both sides
	_server_player.set_multiplayer_authority(_client.multiplayer.get_unique_id())
	_client_player.set_multiplayer_authority(_client.multiplayer.get_unique_id())
	
	# Client now "moves" the player locally
	_client_player.position = CLIENT_MOVE
	
	# Run a few frames. If interpolator is active, it will try to "correct"
	# the position back to START (the last server snapshot).
	for _i in 10:
		await get_tree().process_frame
		
	# Position should remain exactly where the client set it.
	assert_vector(_client_player.position).is_equal(CLIENT_MOVE)
