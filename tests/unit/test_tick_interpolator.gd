## Unit tests for [TickInterpolator].
##
## All timing is driven manually - no real physics loop or network stack needed.
## The test controls exactly which ticks fire and when process frames run,
## making assertions on [member Node2D.position] deterministic.
class_name TestTickInterpolator
extends NetworkedTestSuite


const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)


var _player: Node2D
var _clock: NetworkClock
var _tree: MultiplayerTree
var _interpolator: TickInterpolator
var _sync: MultiplayerSynchronizer


func before_test() -> void:
	# Tree - required for NetwComponent bucket lookups.
	_tree = MultiplayerTree.new()
	add_child(_tree)
	auto_free(_tree)
	
	# Clock - manually driven, not connected to the physics loop.
	_clock = NetworkClock.new()
	_clock.tickrate = 30
	_clock.display_offset = 0  # display_tick = clock.tick; easier reasoning
	_tree.add_child(_clock)
	auto_free(_clock)
	_clock.set_physics_process(false)
	
	var api := _clock.multiplayer as SceneMultiplayer
	assert(api != null, "test requires SceneMultiplayer")
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_network_clock", _clock)
	
	# Remote player - authority 999 ≠ local peer 1.
	_player = Node2D.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(999)
	auto_free(_player)
	
	# Synchronizer - required to avoid tracker warnings.
	_sync = MultiplayerSynchronizer.new()
	_sync.name = "MultiplayerSynchronizer"
	var cfg := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	cfg.property_set_replication_mode(
		ppath, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	_sync.replication_config = cfg
	_sync.set_multiplayer_authority(999)
	_player.add_child(_sync)
	
	# Interpolator
	_interpolator = TickInterpolator.new()
	_interpolator.property_modes = {&"position": TickInterpolator.Mode.LERP}
	_interpolator.enable_smart_dilation = false
	_interpolator.trace_interval = 1
	_player.add_child(_interpolator)
	_interpolator.set_process(false)  # manually driven
	
	# Set owners BEFORE entering tree for discovery
	_sync.owner = _player
	_interpolator.owner = _player
	
	_tree.add_child(_player)
	
	await get_tree().process_frame


func after_test() -> void:
	var api := _clock.multiplayer as SceneMultiplayer
	if api:
		if api.has_meta(&"_network_clock"):
			api.remove_meta(&"_network_clock")
		if api.has_meta(&"_multiplayer_tree"):
			api.remove_meta(&"_multiplayer_tree")


## Advances the clock by one tick, which fires [signal NetworkClock.after_tick]
## and therefore [method TickInterpolator._record_tick].
func _tick() -> void:
	_clock._physics_process(_clock.ticktime)


## Simulates a network packet: sets [member Node2D.position] then runs a process
## frame so the snapshot is captured before the next tick.
func _network_update(pos: Vector2) -> void:
	_player.position = pos
	_interp()


## Triggers a visual interpolation frame manually.
func _interp() -> void:
	_interpolator._update_instance(
		_clock.display_tick, _clock.tick_factor, 0.0, 1.0)


func test_authority_player_position_is_not_modified_by_process() -> void:
	# _process must be a no-op when the owner is the local authority,
	# otherwise the interpolator fights with the player's own _physics_process.
	_player.set_multiplayer_authority(_player.multiplayer.get_unique_id())
	
	_network_update(P0)
	_tick()
	_network_update(P1)
	_tick()
	
	_player.position = P1  # authority set this via move_and_slide
	_interp()
	
	assert_vector(_player.position).is_equal(P1)


func test_empty_buffer_leaves_position_unchanged() -> void:
	# With no ticks fired, the buffer is empty.
	# _process must not touch the position.
	_player.position = P0
	_interp()
	assert_vector(_player.position).is_equal(P0)


func test_buffer_records_network_value_not_interpolated_value() -> void:
	# _process writes a lerped value to owner.position.
	# _record_tick must NOT read that lerped value back into the buffer -
	# it must read the latest network snapshot instead.
	
	_network_update(P0)
	_tick()  # buf records P0 at tick 0; clock.tick -> 1
	_tick()  # buf records P0 again (sparse: skipped); clock.tick -> 2
	
	# Run several render frames - _process writes lerp results to position.
	for _i in 5:
		_interp()
	
	# Now a network packet arrives with P1.
	_network_update(P1)
	_tick()  # buf should record P1 at tick 2, NOT whatever _process last wrote
	
	var buf: HistoryBuffer = _interpolator.get_buffer(&"position")
	assert_that(buf.get_at(2)).is_equal(P1)


func test_position_is_at_p0_before_any_update() -> void:
	# Before the second snapshot arrives, the interpolator can only show P0.
	_network_update(P0)
	_tick()  # buf: {0: P0}
	
	_clock.tick_factor = 0.5
	_interp()
	
	assert_vector(_player.position).is_equal_approx(P0, Vector2(0.1, 0.1))


func test_position_reaches_p1_after_update_tick() -> void:
	# At display_tick = 8 and factor = 1.0, the position must equal P1 exactly.
	const UPDATE_TICK := 8
	_clock.display_offset = UPDATE_TICK  # snapshots are in buffer when displayed
	
	_network_update(P0)
	_tick()  # tick 0: P0 recorded
	
	for _i in UPDATE_TICK - 1:
		_tick()  # ticks 1-7: snapshot unchanged
	
	_network_update(P1)
	_tick()  # tick 8: P1 recorded; clock.tick -> 9
	
	# Drive forward until display_tick = UPDATE_TICK.
	for _i in UPDATE_TICK - 1:
		_tick()
	
	_clock.tick_factor = 0.0
	_interp()
	
	assert_vector(_player.position).is_equal_approx(P1, Vector2(0.5, 0.5))


func test_position_smoothly_interpolates_between_updates() -> void:
	# Given two network snapshots separated by UPDATE_INTERVAL ticks, the
	# displayed position at display_tick == UPDATE_INTERVAL/2 must be close
	# to the midpoint of P0 and P1.
	
	const UPDATE_INTERVAL := 2
	_clock.display_offset = UPDATE_INTERVAL
	
	_network_update(P0)
	_tick()  # tick 0: P0 recorded
	
	for _i in UPDATE_INTERVAL - 1:
		_tick()  # ticks 1: no change
	
	_network_update(P1)
	_tick()  # tick 2: P1 recorded; clock.tick -> 3
	
	# display_tick = clock.tick - display_offset = 3 - 2 = 1.
	# Midway between tick 0 and tick 2.
	_clock.tick_factor = 0.0
	_interp()
	
	var midpoint := P0.lerp(P1, 0.5)
	assert_vector(_player.position).is_equal_approx(midpoint, Vector2(0.1, 0.1))


func test_tick_factor_produces_sub_tick_movement() -> void:
	# tick_factor should produce visible movement within a single tick interval.
	
	const UPDATE_INTERVAL := 2
	_clock.display_offset = UPDATE_INTERVAL
	
	_network_update(P0)
	_tick()  # tick 0
	for _i in UPDATE_INTERVAL - 1:
		_tick()
	
	_network_update(P1)
	_tick()  # tick 2
	
	# clock.tick is now 3, display_tick = 3 - 2 = 1.
	_clock.tick_factor = 0.0
	_interp()
	var pos_at_factor_0 := _player.position.x
	
	_clock.tick_factor = 0.5
	_interp()
	var pos_at_factor_half := _player.position.x
	
	assert_that(pos_at_factor_half > pos_at_factor_0).is_true()
