## Drives the sealed interpolation kernel with no scene, clock, or transport.
##
## The engine's display math is a pure function of recorded snapshots, a per-pump
## timing snapshot, and a spec. This harness supplies all three by hand: it feeds
## a delivery schedule into a real [NetwDisplayHistory], hands the real
## [code]_pump_history[/code] a synthesized [NetwDisplayTiming] every frame, and
## captures the output through a [NetwInterpRecordingWriter]. It calls the same
## kernel methods the live pump calls, so what it certifies is the shipping
## engine, not a reimplementation of it. All intimacy with the engine's private
## shapes lives here, so the calculus tests read as property assertions.
## [codeblock]
## var h := NetwInterpHarness.new()
## h.tickrate = 60.0
## h.configure(NetwInterpolate.new().lerp().smooth(0.05), Vector2.ZERO)
## h.run(oracle, NetwInterpDelivery.wifi(), 5.0, 1234)
## # h.frames / h.displayed / h.playhead_time now hold the run
## [/codeblock]
class_name NetwInterpHarness
extends RefCounted

## Simulation tick rate the sender and display clock run at.
var tickrate := 60.0

## Display frame rate the pump is driven at.
var fps := 60.0

## Ticks between sent snapshots, the snapshot interval.
var send_period := 1

## Wall time in seconds per pumped frame, resolved from [member fps].
var frame_dt_sec := 1.0 / 60.0

## Wall-clock time of each pumped frame.
var frames := PackedFloat64Array()

## Displayed value captured after each frame, held across frames with no write.
var displayed: Array = []

## Reconstructed playhead display time per frame, the monotonicity signal.
var playhead_time := PackedFloat64Array()

## Frames the runtime reported starvation on.
var starving_frames := 0

## Pump stats summed over the whole run, the replay-determinism fingerprint.
var run_stats := NetwPumpStats.new()

## Display offset in ticks the synthesized clock trailed the simulation by.
var display_offset := 0

## Display offset the synthesized clock recommended, feeding the lag floor.
var recommended_display_offset := 0

## Timeline mode the display runs under. FORECAST projects past the newest sample.
var timeline_mode: NetwDisplayHandle.TimelineMode = \
		NetwDisplayHandle.TimelineMode.BUFFERED

## Tick budget the forecast tail projects past the newest sample.
var max_forecast_ticks := 6

## Pump mode the runtime resolves to. BRACKETED is the authored and server path,
## which never forecasts however the handle is set.
var pump_mode := DisplayCore._PUMP_REMOTE

## Wall time after which the stream stops delivering, for the sleep and
## project-to-cap families. Records scheduled past it are dropped.
var record_cutoff_sec := INF

var _iface: DisplayCore
var _rt: DisplayCore._Runtime
var _state: DisplayCore._PropertyState
var _writer: NetwInterpRecordingWriter
var _stats := NetwPumpStats.new()
var _body: _ChaseBody


## Stand-in for the live predicted body a CHASE runtime reads. The chase kernel
## calls [code]source_obj.get(source_prop)[/code] on it, the one live-object read
## the sealed kernel keeps, so a plain object drives it with no node.
class _ChaseBody:
	extends RefCounted

	var value: Variant


## Frame time in seconds, the denominator for per-frame velocity.
func frame_dt() -> float:
	return frame_dt_sec


## The recording writer the run captured through.
func writer() -> NetwInterpRecordingWriter:
	return _writer


## Builds a single-channel remote runtime displaying [param spec], starting the
## displayed value at [param initial].
func configure(spec: NetwInterpolate, initial: Variant) -> void:
	frame_dt_sec = 1.0 / fps
	_iface = DisplayCore.new()

	_rt = DisplayCore._Runtime.new()
	_rt.config = DisplayCore._Config.new()
	_rt.config.timeline_mode = timeline_mode
	_rt.config.max_forecast_ticks = max_forecast_ticks
	_rt.playhead = DisplayCore._Playhead.new()
	_rt.playhead.expected_interval_ticks = maxi(1, send_period)
	_rt.pump_mode = pump_mode

	_state = DisplayCore._PropertyState.new()
	_state.name = &"value"
	_state.spec = spec
	_state.source_prop = &"value"
	_state.target_prop = &"value"
	_state.history = NetwDisplayHistory.new()
	_state.history.mode = spec.mode
	_state.history.snap_distance = spec.snap_distance
	_writer = NetwInterpRecordingWriter.new()
	_state.output = _writer
	_state.last_written = initial

	_rt.states.append(_state)
	_rt.states_by_key[_state.name] = _state


## Builds a single-channel predicted CHASE runtime that eases toward a live body
## with exponential [param smooth_time], starting the display at [param initial].
func configure_chase(spec: NetwInterpolate, smooth_time: float, initial: Variant) -> void:
	frame_dt_sec = 1.0 / fps
	_iface = DisplayCore.new()

	_rt = DisplayCore._Runtime.new()
	_rt.config = DisplayCore._Config.new()
	_rt.config.predicted_smooth_time = smooth_time
	_rt.playhead = DisplayCore._Playhead.new()
	_rt.pump_mode = DisplayCore._PUMP_CHASE

	_body = _ChaseBody.new()
	_body.value = initial

	_state = DisplayCore._PropertyState.new()
	_state.name = &"value"
	_state.spec = spec
	_state.source_obj = _body
	_state.source_prop = &"value"
	_state.target_prop = &"value"
	_state.history = NetwDisplayHistory.new()
	_state.history.mode = spec.mode
	_writer = NetwInterpRecordingWriter.new()
	_state.output = _writer
	_state.last_written = initial

	_rt.states.append(_state)


## Runs the CHASE display against a live body that snaps to [param oracle] once
## per tick, the stepwise input the chase filter must smooth. Fills the same
## result series [method run] does.
func run_chase(oracle: NetwInterpOracle, duration_sec: float) -> void:
	frames = PackedFloat64Array()
	displayed = []
	playhead_time = PackedFloat64Array()
	starving_frames = 0

	var frame_count := int(ceil(duration_sec * fps))
	for f in frame_count:
		var wall := float(f) / fps
		# The body advances at tick cadence, so the display input is a staircase
		# and any smoothness in the output is the chase filter's doing.
		var tick := int(floor(wall * tickrate))
		_body.value = oracle.value_at(float(tick) / tickrate)

		var timing := _timing(wall)
		_writer.mark_frame(wall)
		_stats.reset()
		_iface._pump_chase(_rt, timing, _stats)

		frames.append(wall)
		displayed.append(_state.last_written)
		playhead_time.append(wall)


## Writes the live chase body's value directly, the manual counterpart of the
## oracle staircase [method run_chase] drives.
func set_body(value: Variant) -> void:
	_body.value = value


## Feeds one recovery into the chase exactly the way the live absorber does,
## so a calculus law can assert what a correction looks like on the display.
func absorb_recovery(deltas: Dictionary, teleported: bool = false) -> void:
	_iface._on_chase_recovered(0, deltas, teleported, 0, _rt)


## Pumps one chase frame at [param wall] seconds, recording it the way
## [method run_chase] does, so a law can interleave body writes, recoveries,
## and frames by hand.
func step_chase(wall: float) -> void:
	var timing := _timing(wall)
	_writer.mark_frame(wall)
	_stats.reset()
	_iface._pump_chase(_rt, timing, _stats)
	frames.append(wall)
	displayed.append(_state.last_written)
	playhead_time.append(wall)


## A warmup window long enough for the buffer to prime and the lag to settle,
## given the display offset the run's preset implied.
func warmup_hint() -> float:
	return float(display_offset) / tickrate + 0.5


## Runs [param oracle] over [param preset] for [param duration_sec], seeded by
## [param seed]. Fills [member frames], [member displayed], and
## [member playhead_time].
func run(
		oracle: NetwInterpOracle,
		preset: NetwInterpDelivery.Preset,
		duration_sec: float,
		seed: int,
) -> void:
	var ticktime := 1.0 / tickrate
	var latency_ticks := preset.latency_ms * 0.001 * tickrate
	var jitter_ticks := preset.jitter_ms * 0.001 * tickrate
	display_offset = ceili(latency_ticks)
	recommended_display_offset = display_offset + ceili(jitter_ticks)

	var delivery := NetwInterpDelivery.new(preset, seed)
	var schedule := delivery.build(oracle, tickrate, send_period, duration_sec)

	# Prime the lag floor the way _reset_runtime does before the first pump.
	var seed_timing := _timing(0.0)
	var floor_lag := _iface._calculate_min_lag(_rt, seed_timing)
	_rt.playhead.smoothed_floor = floor_lag
	_rt.playhead.display_lag = floor_lag

	frames = PackedFloat64Array()
	displayed = []
	playhead_time = PackedFloat64Array()
	starving_frames = 0
	run_stats.reset()

	var frame_count := int(ceil(duration_sec * fps))
	var next_arrival := 0
	for f in frame_count:
		var wall := float(f) / fps
		while next_arrival < schedule.size() \
				and schedule[next_arrival].arrival_sec <= wall:
			var a := schedule[next_arrival]
			if a.arrival_sec <= record_cutoff_sec:
				_state.history.record(a.tick, a.value, false)
			next_arrival += 1

		var timing := _timing(wall)
		_writer.mark_frame(wall)
		_stats.reset()
		_iface._pump_history(_rt, timing, _stats)
		run_stats.merge(_stats)

		frames.append(wall)
		displayed.append(_state.last_written)
		playhead_time.append(
			float(timing.display_tick) + timing.tick_factor
			- _rt.playhead.display_lag,
		)
		if _stats.starving > 0:
			starving_frames += 1


func _timing(wall: float) -> NetwDisplayTiming:
	var timing := NetwDisplayTiming.new()
	var sim_ticks := wall * tickrate
	var display_time := sim_ticks - float(display_offset)
	timing.tick = int(floor(sim_ticks))
	timing.display_tick = int(floor(display_time))
	timing.tick_factor = display_time - float(timing.display_tick)
	timing.ticktime = 1.0 / tickrate
	timing.display_offset = display_offset
	timing.recommended_display_offset = recommended_display_offset
	timing.frame_delta = frame_dt_sec
	timing.frame_ticks = frame_dt_sec * tickrate
	return timing
