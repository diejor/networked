## Drives several interpolation runtimes through one pump loop in a chosen order.
##
## The engine partitions its state per entity: a runtime touches only its own
## history and playhead, so the order the pump visits runtimes cannot change any
## one runtime's output. This harness makes that claim testable. It builds N
## independent remote lanes, feeds each its own delivery stream, and pumps them
## every frame in a caller-supplied permutation. Two runs whose only difference
## is that permutation must emit identical per-lane sequences and identical merged
## stats, which is the operational form of the factorization the native fork-join
## later leans on.
## [codeblock]
## var s := NetwInterpScheduleHarness.new()
## s.add_lane(oracle_a, NetwInterpDelivery.wifi(), 1)
## s.add_lane(oracle_b, NetwInterpDelivery.poor_3g(), 2)
## s.run(5.0, PackedInt32Array([0, 1]))       # registration order
## var seq0 := s.lane(0).displayed             # lane 0's displayed sequence
## [/codeblock]
class_name NetwInterpScheduleHarness
extends RefCounted

## Simulation tick rate every lane's clock runs at.
var tickrate := 60.0

## Display frame rate the shared pump loop runs at.
var fps := 60.0

## Ticks between sent snapshots on every lane.
var send_period := 1

## Pump stats summed across every lane and frame, the schedule-invariant merge.
var run_stats := NetwPumpStats.new()

var _core: NetwMultiplayerCore
var _lanes: Array[Lane] = []
var _stats := NetwPumpStats.new()


## One independent runtime: its truth, its link, its captured output.
class Lane:
	extends RefCounted

	var oracle: NetwInterpOracle
	var preset: NetwInterpDelivery.Preset
	var seed: int
	var display_offset := 0
	var recommended_display_offset := 0

	var rt: NetwDisplayRuntime
	var state: NetwDisplayChannel
	var writer: NetwInterpRecordingWriter
	var schedule: Array[NetwInterpDelivery.Arrival] = []
	var next_arrival := 0

	## Displayed value captured after each frame.
	var displayed: Array = []


## Registers a lane displaying [param oracle] over [param preset], seeded by
## [param seed]. Lanes pump in registration order unless a permutation overrides it.
func add_lane(oracle: NetwInterpOracle, preset: NetwInterpDelivery.Preset, seed: int) -> void:
	var lane := Lane.new()
	lane.oracle = oracle
	lane.preset = preset
	lane.seed = seed
	_lanes.append(lane)


## The lane registered at [param index].
func lane(index: int) -> Lane:
	return _lanes[index]


## Runs every lane for [param duration_sec], visiting them each frame in
## [param order] (a permutation of lane indices). Fills each lane's
## [member Lane.displayed] and the shared [member run_stats].
func run(duration_sec: float, order: PackedInt32Array) -> void:
	_core = NetwMultiplayerCore.new()
	run_stats.reset()
	var ticktime := 1.0 / tickrate

	for lane in _lanes:
		var latency_ticks := lane.preset.latency_ms * 0.001 * tickrate
		var jitter_ticks := lane.preset.jitter_ms * 0.001 * tickrate
		lane.display_offset = ceili(latency_ticks)
		lane.recommended_display_offset = lane.display_offset + ceili(jitter_ticks)

		lane.rt = NetwDisplayRuntime.new()
		lane.rt.config = NetwDisplayDecl.new()
		lane.rt.playhead = NetwDisplayPlayhead.new()
		lane.rt.playhead.expected_interval_ticks = maxi(1, send_period)
		lane.rt.pump_mode = NetwDisplayDecl.PUMP_REMOTE

		lane.state = NetwDisplayChannel.new()
		lane.state.name = &"value"
		lane.state.spec = NetwInterpolate.new().lerp().smooth(0.05).to(&"value")
		lane.state.source_prop = &"value"
		lane.state.target_prop = &"value"
		lane.state.history = NetwDisplayHistory.new()
		lane.state.history.mode = lane.state.spec.mode
		lane.writer = NetwInterpRecordingWriter.new()
		lane.state.output = lane.writer.write
		lane.state.last_written = lane.oracle.value_at(0.0)
		lane.rt.states.append(lane.state)

		var delivery := NetwInterpDelivery.new(lane.preset, lane.seed)
		lane.schedule = delivery.build(lane.oracle, tickrate, send_period, duration_sec)
		lane.next_arrival = 0
		lane.displayed = []

		lane.rt.playhead.settle(
			lane.rt.config,
			lane.display_offset,
			lane.recommended_display_offset,
		)

	var frame_count := int(ceil(duration_sec * fps))
	for f in frame_count:
		var wall := float(f) / fps
		for lane in _lanes:
			while lane.next_arrival < lane.schedule.size() \
					and lane.schedule[lane.next_arrival].arrival_sec <= wall:
				var a := lane.schedule[lane.next_arrival]
				lane.state.history.record(a.tick, a.value, false)
				lane.next_arrival += 1

		for idx in order:
			var lane := _lanes[idx]
			var timing := _timing(lane, wall)
			lane.writer.mark_frame(wall)
			_stats.reset()
			_core.display_pump_runtime(lane.rt, timing, _stats)
			run_stats.merge(_stats)
			lane.displayed.append(lane.state.last_written)


func _timing(lane: Lane, wall: float) -> NetwDisplayTiming:
	var timing := NetwDisplayTiming.new()
	var sim_ticks := wall * tickrate
	var display_time := sim_ticks - float(lane.display_offset)
	timing.tick = int(floor(sim_ticks))
	timing.display_tick = int(floor(display_time))
	timing.tick_factor = display_time - float(timing.display_tick)
	timing.ticktime = 1.0 / tickrate
	timing.display_offset = lane.display_offset
	timing.recommended_display_offset = lane.recommended_display_offset
	timing.frame_delta = 1.0 / fps
	timing.frame_ticks = tickrate / fps
	return timing
