## Unit tests for [TickInterpolator] signal-based injection and visual decoupling.
class_name TestTickInterpolatorSignals
extends NetworkedTestSuite


const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)


var _player: Node2D
var _visual: Node2D
var _clock: NetworkClock
var _tree: MultiplayerTree
var _interpolator: TickInterpolator
var _sync: MultiplayerSynchronizer


func before_test() -> void:
	_tree = MultiplayerTree.new()
	add_child(_tree)
	auto_free(_tree)

	_clock = NetworkClock.new()
	_clock.tickrate = 30
	_clock.display_offset = 0
	_tree.add_child(_clock)
	auto_free(_clock)
	_clock.set_physics_process(false)

	var api := _clock.multiplayer as SceneMultiplayer
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_network_clock", _clock)

	_player = Node2D.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(999)
	auto_free(_player)

	_visual = Node2D.new()
	_visual.name = "Visual"
	_player.add_child(_visual)

	_sync = MultiplayerSynchronizer.new()
	_sync.name = "MultiplayerSynchronizer"
	var cfg := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	_sync.replication_config = cfg
	_sync.set_multiplayer_authority(999)
	_player.add_child(_sync)

	_interpolator = TickInterpolator.new()
	_interpolator.property_modes = {&"position": TickInterpolator.Mode.LERP}
	_interpolator.enable_smart_dilation = false
	_interpolator.trace_interval = 1
	_player.add_child(_interpolator)
	_interpolator.set_process(false)

	# Set owners BEFORE tree entry
	_sync.owner = _player
	_interpolator.owner = _player

	add_child(_player)

	await get_tree().process_frame


func after_test() -> void:
	var api := _clock.multiplayer as SceneMultiplayer
	if api:
		api.remove_meta(&"_network_clock")
		api.remove_meta(&"_multiplayer_tree")


func _tick() -> void:
	_clock._physics_process(_clock.ticktime)


func test_signal_injection_records_immediately() -> void:
	## When using signals, the snapshot should be recorded immediately into history
	## without waiting for the next clock tick or a polling frame.
	
	# Initial state
	_player.position = P0
	_tick() # clock.tick is now 1
	
	# Simulate network sync
	_player.position = P1
	_sync.synchronized.emit() # records at clock.tick = 1
	
	var buf := _interpolator.get_buffer(&"position")
	# Should have P1 at current clock tick (1)
	assert_vector(buf.get_at(1)).is_equal(P1)


func test_signal_injection_prevents_overwrite() -> void:
	## This tests the core fix for the "one-frame jump".
	## Even if we run _process in the same frame as the sync, it should see
	## the new snapshot in history and NOT overwrite player position with stale history.
	
	_player.position = P0
	_tick() # clock.tick is now 1
	
	# 1. Sync arrives
	_player.position = P1
	_sync.synchronized.emit() # recorded at tick 1
	
	# 2. Interpolator runs in same frame
	# clock.display_tick = 1 (if display_offset=0), factor = 0.0 -> should read P1 from history
	_clock.display_offset = 0
	_clock.tick_factor = 0.0
	
	# Trigger internal logic directly since we don't have a peer batcher in this test
	_interpolator._update_instance(_clock.display_tick, _clock.tick_factor, 0.0, 1.0)
	
	assert_vector(_player.position).is_equal(P1)


func test_visual_root_decoupling() -> void:
	## When visual_root is set, interpolation writes the absolute smooth value 
	## to the child, while the parent (physics body) keeps the raw network position.
	
	# Setup visual root BEFORE recording snapshots
	_interpolator.visual_root = NodePath("../Visual")
	_visual.position = Vector2.ZERO
	_interpolator.reset()
	
	_player.position = P0
	_sync.synchronized.emit() # recorded at tick 0
	
	_tick() # clock.tick -> 1
	
	_player.position = P1
	_sync.synchronized.emit() # recorded at tick 1
	
	# We want to display midpoint between tick 0 and tick 1.
	# display_tick = 0, factor = 0.5.
	_clock.display_offset = 1 # clock.tick(1) - 1 = 0
	_clock.tick_factor = 0.5
	
	_interpolator._update_instance(_clock.display_tick, _clock.tick_factor, 0.0, 1.0)
	
	# Parent remains at raw P1 (latest set by sync)
	assert_vector(_player.position).is_equal(P1)
	
	# Visual child is at midpoint P0.5 (relative to raw parent)
	# global_interpolated = P0.lerp(P1, 0.5) = (50, 0)
	# global_parent = P1 = (100, 0)
	# relative_offset = (50, 0) - (100, 0) = (-50, 0)
	assert_vector(_visual.position).is_equal_approx(P0.lerp(P1, 0.5) - P1, Vector2(0.1, 0.1))


func test_polling_is_disabled_when_signals_available() -> void:
	## If a property is covered by a synchronizer, polling should NOT happen.
	## We can verify this by manually changing position WITHOUT emitting signal,
	## it should NOT be captured.
	
	_player.position = P0
	_sync.synchronized.emit() # tick 0
	_tick() # clock.tick -> 1
	
	# Manual change without signal
	_player.position = P1
	_interpolator._update_instance(_clock.display_tick, _clock.tick_factor, 0.0, 1.0)
	
	var buf := _interpolator.get_buffer(&"position")
	# buf should still only have P0 from tick 0
	assert_vector(buf.get_at(0)).is_equal(P0)
	assert_that(buf.get_at(1)).is_null()
