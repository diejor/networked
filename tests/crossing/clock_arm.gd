## The clock family's Bridge-B arm: drives the GDScript tick engine through
## scripted schedules and compares what it observes to the committed golden.
##
## This is the instrument, not a test. The clock has no seam and no byte
## grammar, so the only thing that can license deleting its GDScript arm is a
## trace recorded from that arm BEFORE its replacement existed. Recording it
## afterwards would describe what the port produced rather than what it must
## reproduce, and no amount of later work recovers that.
##
## [br][br]
## Every scenario drives [ClockCore] alone, with no session, no node and no real
## time, so the whole trace is a function of the calls the arm makes. That is
## also why it reaches no private: [method ClockCore.handle_pong] answers the
## whole calibration result as a metrics dictionary, and the effect of the
## [constant ClockCore.SyncMode.STRETCH] anchor is read off the ticks that follow
## it rather than off the anchor itself. Behaviour is stronger evidence than
## internal state, and it is evidence the port cannot quietly redefine.
## [codeblock]
## godot --headless --path . -s res://tests/crossing/clock_arm.gd
## godot --headless --path . -s res://tests/crossing/clock_arm.gd -- --record
## [/codeblock]
##
## Two readings are deliberately uncovered because neither is a function of the
## calls alone. [member ClockCore.tick_factor] and [method ClockCore.cadence]
## both read the wall clock, so they belong to a native restatement that can pin
## time, not to a golden. Everything else the engine exposes is here.
##
## Exits 1 on the first differing row, naming the scenario and both sides.
extends SceneTree

const Recorder := preload("res://tests/support/netw_recorder.gd")

const GOLDEN := "res://tests/native/goldens/clock_schedules.txt"

## Digits a continuous reading is written to. RTT, jitter and accumulated phase
## are genuinely continuous, so they get a declared precision rather than an
## exact claim, and every scenario's inputs are chosen to land far from a
## rounding boundary at this width.
const PRECISION := 6

## A tickrate whose tick period is exactly representable in binary, and the
## fractions of it every scheduled delta is built from.
##
## The accumulator decides whether to emit a tick by comparing against the tick
## period, so a schedule assembled out of values that only approximate the
## period can land either side of that comparison on a different machine or a
## different compiler. Every delta below is a whole multiple of a power of two
## divided by this rate, which makes each comparison exact rather than close.
const EXACT_RATE := 8
const TICK := 0.125
const HALF_TICK := 0.0625
const QUARTER_TICK := 0.03125

## The rate the phase scenario runs at, chosen the same way: a quarter of this
## tick period is exact, so four of them sum to the period with no residue.
const PHASE_RATE := 32

const WATCHED: Array[StringName] = [
	&"before_tick_loop",
	&"before_tick",
	&"on_tick",
	&"after_tick",
	&"after_tick_loop",
	&"clock_synchronized",
	&"stability_changed",
	&"display_offset_insufficient",
]

# Every scenario in the order the golden holds them.
const SCENARIOS: Array[StringName] = [
	&"tick/accumulates_to_whole_ticks",
	&"tick/the_ceiling_caps_a_long_frame",
	&"tick/a_stall_resets_the_accumulator",
	&"tick/force_step_matches_the_loop",
	&"gate/ungated_simulates_every_frame",
	&"gate/gated_holds_the_frames_that_run_no_tick",
	&"gate/two_steps_per_tick_admits_two_frames",
	&"gate/gates_nest_and_only_the_last_release_ungates",
	&"gate/a_peer_that_cannot_keep_up_reports_it",
	&"sync/the_first_pong_synchronizes_and_seeds_the_phase",
	&"sync/snap_jumps_where_stretch_crawls",
	&"sync/the_target_follows_the_server_phase",
	&"sync/jitter_moves_stability_and_the_recommendation",
]

var _rows: Array[String] = []
var _clock: ClockCore
var _recorder: RefCounted


func _initialize() -> void:
	if "--record" in OS.get_cmdline_user_args():
		var rows := _run_all()
		var file := FileAccess.open(GOLDEN, FileAccess.WRITE)
		assert(file != null, "cannot write golden: %s" % GOLDEN)
		file.store_string(_header() + "\n".join(rows) + "\n")
		print("ARM recorded %d rows to %s" % [rows.size(), GOLDEN])
		quit(0)
		return
	quit(_compare())


func _compare() -> int:
	var expected := _read_golden()
	var actual := _run_all()
	if expected.is_empty():
		printerr("ARM golden is empty: %s" % GOLDEN)
		return 1

	var limit := maxi(expected.size(), actual.size())
	for index in limit:
		var want := expected[index] if index < expected.size() else "<missing>"
		var got := actual[index] if index < actual.size() else "<missing>"
		if want == got:
			continue
		printerr(
			"DIFFER at row %d\n  golden %s\n  arm    %s" % [index, want, got]
		)
		printerr("ARM %d of %d rows match" % [index, expected.size()])
		return 1

	print("ARM %d rows match %s" % [actual.size(), GOLDEN])
	return 0


func _run_all() -> Array[String]:
	_rows = []
	# physics_steps_per_tick divides the engine's physics rate, so the rate is an
	# antecedent of every gate scenario rather than a constant of the trace.
	_rows.append(
		"antecedent||physics_ticks_per_second=%d"
		% Engine.physics_ticks_per_second
	)
	for scenario in SCENARIOS:
		match scenario:
			&"tick/accumulates_to_whole_ticks":
				_accumulates_to_whole_ticks(scenario)
			&"tick/the_ceiling_caps_a_long_frame":
				_the_ceiling_caps_a_long_frame(scenario)
			&"tick/a_stall_resets_the_accumulator":
				_a_stall_resets_the_accumulator(scenario)
			&"tick/force_step_matches_the_loop":
				_force_step_matches_the_loop(scenario)
			&"gate/ungated_simulates_every_frame":
				_ungated_simulates_every_frame(scenario)
			&"gate/gated_holds_the_frames_that_run_no_tick":
				_gated_holds_the_frames_that_run_no_tick(scenario)
			&"gate/two_steps_per_tick_admits_two_frames":
				_two_steps_per_tick_admits_two_frames(scenario)
			&"gate/gates_nest_and_only_the_last_release_ungates":
				_gates_nest_and_only_the_last_release_ungates(scenario)
			&"gate/a_peer_that_cannot_keep_up_reports_it":
				_a_peer_that_cannot_keep_up_reports_it(scenario)
			&"sync/the_first_pong_synchronizes_and_seeds_the_phase":
				_the_first_pong_synchronizes_and_seeds_the_phase(scenario)
			&"sync/snap_jumps_where_stretch_crawls":
				_snap_jumps_where_stretch_crawls(scenario)
			&"sync/the_target_follows_the_server_phase":
				_the_target_follows_the_server_phase(scenario)
			&"sync/jitter_moves_stability_and_the_recommendation":
				_jitter_moves_stability_and_the_recommendation(scenario)
			_:
				printerr("unknown scenario: %s" % scenario)
	return _rows


#region Tick loop

# A frame shorter than a tick buys none, and the leftover is kept rather than
# dropped, so two half-frames are worth exactly one tick.
func _accumulates_to_whole_ticks(scenario: StringName) -> void:
	_open(EXACT_RATE)
	for delta in [HALF_TICK, HALF_TICK, TICK, QUARTER_TICK]:
		_clock.physics_step(delta)
		_row(scenario, "frame", { &"delta": delta, &"tick": _clock.tick })
		_signals(scenario)
	_close()


# The ceiling is a ceiling and not a target: a frame carrying more time than it
# may spend emits exactly the ceiling and keeps the rest.
func _the_ceiling_caps_a_long_frame(scenario: StringName) -> void:
	_open(EXACT_RATE)
	_clock.max_ticks_per_frame = 2
	_clock.physics_step(TICK * 5.0)
	_row(scenario, "capped", { &"tick": _clock.tick })
	_signals(scenario)

	# The residue is still owed, so the next empty frame keeps drawing on it.
	_clock.physics_step(0.0)
	_row(scenario, "residue", { &"tick": _clock.tick })
	_close()


# A frame longer than the stall threshold is a hitch rather than simulated time.
# What the reset discards is the time banked BEFORE it, which is why the law
# needs the two arms below: the same stalling frame pays out differently
# depending on whether a partial tick was already owed.
func _a_stall_resets_the_accumulator(scenario: StringName) -> void:
	# Both arms run the IDENTICAL schedule and differ only in where the
	# threshold sits, so whatever the counts differ by is the reset and nothing
	# else. The long frame pays out five ticks either way; what it cannot pay
	# out twice is the half tick banked before it, and that only becomes visible
	# on the frame after, which is why there is one.
	for threshold in [TICK * 6.0, TICK * 4.0]:
		_open(EXACT_RATE)
		_clock.max_ticks_per_frame = 100
		_clock.stall_threshold = threshold

		_clock.physics_step(HALF_TICK)
		_clock.physics_step(TICK * 5.0)
		_row(
			scenario,
			"long_frame",
			{ &"stalled": TICK * 5.0 > threshold, &"tick": _clock.tick },
		)

		_clock.physics_step(HALF_TICK)
		_row(
			scenario,
			"frame_after",
			{ &"stalled": TICK * 5.0 > threshold, &"tick": _clock.tick },
		)
	_signals(scenario)
	_close()


# The manual seam has to mean the same thing as the loop it replaces, or every
# stepped test measures a second implementation. What differs is the loop
# bracket: force_step is a step rather than a frame, so it emits no tick-loop
# signal, and that difference is a law rather than an oversight.
func _force_step_matches_the_loop(scenario: StringName) -> void:
	_open(EXACT_RATE)
	_clock.force_step(3)
	_row(scenario, "stepped", { &"tick": _clock.tick })
	_signals(scenario)

	_clock.force_step(0)
	_row(scenario, "zero", { &"tick": _clock.tick })
	_signals(scenario)

	_open(EXACT_RATE)
	_clock.physics_step(TICK * 3.0)
	_row(scenario, "pumped", { &"tick": _clock.tick })
	_signals(scenario)
	_close()

#endregion

#region Simulation gate

# Frames a clock admits while `ticks_per_frame` names the ticks each frame
# emitted, which is how a throttled clock is described as the pattern it
# actually produces.
func _admitted(scenario: StringName, ticks_per_frame: Array) -> void:
	for ticks: int in ticks_per_frame:
		_clock.force_step(ticks)
		_row(
			scenario,
			"frame",
			{
				&"ticks": ticks,
				&"simulating": _clock.is_simulating,
				&"behind": _clock.simulation_behind_count,
			},
		)


# A game that predicts no engine-integrated body must never see a held frame.
func _ungated_simulates_every_frame(scenario: StringName) -> void:
	_open(60)
	_admitted(scenario, [1, 0, 1, 0, 0, 1])
	_close()


# A gated frame that emitted no tick bought no simulated time, so it must not
# advance the world.
func _gated_holds_the_frames_that_run_no_tick(scenario: StringName) -> void:
	_open(60)
	_clock.arm_gate()
	_admitted(scenario, [1, 0, 1, 1, 0, 1])
	_close()


# A tick worth two steps pays for the frame that emitted it and the one after,
# so the correspondence stays exact rather than rounding.
func _two_steps_per_tick_admits_two_frames(scenario: StringName) -> void:
	_open(Engine.physics_ticks_per_second / 2)
	_row(
		scenario,
		"budget",
		{ &"steps_per_tick": _clock.physics_steps_per_tick },
	)
	_clock.arm_gate()
	_admitted(scenario, [1, 0, 1, 0, 0, 1])
	_close()


# Two predicted bodies share one clock and either may leave first, so gates nest
# and only the last release gives the world its frames back.
func _gates_nest_and_only_the_last_release_ungates(scenario: StringName) -> void:
	_open(60)
	_clock.arm_gate()
	_clock.arm_gate()

	_clock.release_gate()
	_row(scenario, "one_released", { &"gated": _clock.is_gated() })
	_clock.force_step(0)
	_row(scenario, "held", { &"simulating": _clock.is_simulating })

	_clock.release_gate()
	_row(
		scenario,
		"all_released",
		{ &"gated": _clock.is_gated(), &"simulating": _clock.is_simulating },
	)
	_close()


# The honest ceiling. A gated clock emits one tick per frame at most, so a peer
# that cannot sustain the rate falls behind and says so rather than doubling up
# — and falling behind must not also stop the world, or it can never catch up.
func _a_peer_that_cannot_keep_up_reports_it(scenario: StringName) -> void:
	_open(60)
	_clock.arm_gate()
	_clock.physics_step(_clock.ticktime * 3.0)
	_row(
		scenario,
		"behind",
		{
			&"count": _clock.simulation_behind_count,
			&"simulating": _clock.is_simulating,
		},
	)

	_open(60)
	_clock.physics_step(_clock.ticktime * 3.0)
	_row(
		scenario,
		"ungated_never_reports",
		{ &"count": _clock.simulation_behind_count },
	)
	_close()

#endregion

#region Calibration

# The first pong hard-aligns so STRETCH begins already converged, and it seeds
# the phase within the tick rather than landing on the boundary below it.
func _the_first_pong_synchronizes_and_seeds_the_phase(
		scenario: StringName,
) -> void:
	_open(30)
	_clock.sync_mode = ClockCore.SyncMode.STRETCH
	_clock.lead_ticks = 0.0
	_row(scenario, "before", { &"synchronized": _clock.is_synchronized })

	_metrics(scenario, "first", _clock.handle_pong(0.0, 40, 0.5))
	_signals(scenario)

	# A second pong must not re-announce a synchronization that already happened.
	_metrics(scenario, "second", _clock.handle_pong(0.0, 41, 0.5))
	_signals(scenario)
	_close()


# SNAP takes the whole correction at once. STRETCH takes a fraction of it per
# frame, so the same divergence resolves as a crawl and never as a teleport.
func _snap_jumps_where_stretch_crawls(scenario: StringName) -> void:
	_open(PHASE_RATE)
	_clock.sync_mode = ClockCore.SyncMode.SNAP
	_clock.lead_ticks = 0.0
	_clock.handle_pong(0.0, 100, 0.0)
	_clock.handle_pong(0.0, 150, 0.0)
	_row(scenario, "snapped", { &"tick": _clock.tick })

	_open(PHASE_RATE)
	_clock.sync_mode = ClockCore.SyncMode.STRETCH
	_clock.lead_ticks = 0.0
	_clock.stretch_nudge_factor = 0.5
	_clock.panic_snap_threshold = 100
	_clock.handle_pong(0.0, 100, 0.0)
	_clock.handle_pong(0.0, 104, 0.0)
	for step in 4:
		_clock.physics_step(0.0)
		_row(scenario, "crawled", { &"step": step, &"tick": _clock.tick })

	# A divergence past the panic threshold is a real desync, so it snaps rather
	# than crawling for a second at a time.
	_clock.panic_snap_threshold = 2
	_clock.handle_pong(0.0, 400, 0.0)
	_clock.physics_step(0.0)
	_row(scenario, "panicked", { &"tick": _clock.tick })
	_close()


# The target tracks the server's continuous position, so a pong arriving a
# fraction of a tick later seeds the clock that fraction further into its tick
# and never a whole tick further. A quantized anchor would instead flip by one
# tick as the arrival phase crossed a server boundary, and
# [constant ClockCore.SyncMode.STRETCH] would chase each flip for about a
# second, sweeping the client's tick boundary through the server's.
#
# The seeded phase is sub-tick by construction, so reading the tick counter
# straight after the pong says 101 for every phase and proves nothing. What the
# phase actually buys is EARLIER ARRIVAL AT THE NEXT BOUNDARY, so the reading is
# a schedule: four quarter-tick frames, and which of them emits. Each phase
# produces a different signature, which is the continuity a single reading
# cannot show.
func _the_target_follows_the_server_phase(scenario: StringName) -> void:
	var quarter := 1.0 / float(PHASE_RATE) / 4.0
	for phase in [0.0, 0.25, 0.5, 0.75]:
		_open(PHASE_RATE)
		# SNAP, so the frames below only accumulate and never also nudge.
		_clock.sync_mode = ClockCore.SyncMode.SNAP
		_clock.lead_ticks = 1.0
		_clock.handle_pong(0.0, 100, phase)
		_row(scenario, "seeded", { &"phase": phase, &"tick": _clock.tick })

		var schedule: Array[int] = []
		for _step in 4:
			_clock.physics_step(quarter)
			schedule.append(_clock.tick)
		_row(scenario, "schedule", { &"phase": phase, &"ticks": schedule })
	_close()


# Jitter is the mean absolute deviation of the sample window. It decides
# stability, and it widens the display offset the clock recommends.
func _jitter_moves_stability_and_the_recommendation(
		scenario: StringName,
) -> void:
	_open(30)
	_clock.jitter_window = 4
	_clock.jitter_stability_threshold = 0.01
	_clock.jitter_multiplier = 2.0
	_clock.display_offset = 1

	for sample in [0.020000, 0.020000, 0.200000, 0.020000]:
		_metrics(scenario, "sample", _clock.handle_pong(sample, 100, 0.0))
	_signals(scenario)
	_close()

#endregion

#region Rig

func _open(tickrate: int) -> void:
	_clock = ClockCore.new()
	_clock.tickrate = tickrate
	_recorder = Recorder.new(_clock, WATCHED)


func _close() -> void:
	_recorder = null
	_clock = null


# The signal order since the last reading, which is what polled state cannot
# express and what every tick-loop law is actually about.
func _signals(scenario: StringName) -> void:
	_row(scenario, "signals", { &"order": _recorder.order() })
	_recorder.clear()


# The whole calibration result, which handle_pong answers as one payload, so the
# arm reads the engine's own summary rather than a reconstruction of it.
func _metrics(scenario: StringName, label: String, payload: Dictionary) -> void:
	_row(
		scenario,
		"pong",
		{
			&"at": label,
			&"diff": payload["diff"],
			&"is_stable": payload["is_stable"],
			&"is_synchronized": payload["is_synchronized"],
			&"recommended": payload["recommended_display_offset"],
			&"rtt_avg": payload["rtt_avg"],
			&"rtt_jitter": payload["rtt_jitter"],
			&"tick": payload["tick"],
		},
	)

#endregion

#region Rows

# One observation. Keys are sorted as text so a dictionary literal's authoring
# order can never move a golden. Sorting StringNames directly would not do it:
# they compare by their interned address, which is allocation order rather than
# spelling, and it is stable enough to look correct and not stable enough to be.
func _row(scenario: StringName, kind: String, fields: Dictionary) -> void:
	var keys: Array[String] = []
	for key in fields:
		keys.append(String(key))
	keys.sort()
	var parts: Array[String] = []
	for key in keys:
		parts.append("%s=%s" % [key, _value(fields[StringName(key)])])
	_rows.append("%s|%s|%s" % [scenario, kind, " ".join(parts)])


func _value(value: Variant) -> String:
	if value is bool:
		return "true" if value else "false"
	if value is float:
		return _real(value)
	if value is Array or value is PackedInt32Array:
		var parts: Array[String] = []
		for item in value:
			parts.append(_value(item) if item is float else str(item))
		return "[%s]" % ",".join(parts)
	return str(value)


# A continuous reading at the declared precision, padded so the width is the
# claim rather than a coincidence of the value, and with negative zero
# normalized so a sign that carries no information cannot move a row.
func _real(value: float) -> String:
	var text := "%.*f" % [PRECISION, value]
	return text.substr(1) if text.begins_with("-0.000000") else text


func _header() -> String:
	return (
		"# The clock family's Bridge-B trace, recorded from the GDScript tick\n"
		+ "# engine before any of it was native. Rows are\n"
		+ "# scenario|kind|key=value, keys sorted, continuous readings at 6\n"
		+ "# digits, tick-domain values exact.\n"
		+ "#\n"
		+ "# Regenerate with:\n"
		+ "#   godot --headless --path . -s res://tests/crossing/clock_arm.gd \\\n"
		+ "#     -- --record\n"
		+ "# Regenerating against a candidate implementation destroys the\n"
		+ "# evidence this file exists to be.\n"
	)


func _read_golden() -> Array[String]:
	var file := FileAccess.open(GOLDEN, FileAccess.READ)
	assert(file != null, "missing golden: %s" % GOLDEN)
	var lines: Array[String] = []
	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		lines.append(line)
	return lines

#endregion
