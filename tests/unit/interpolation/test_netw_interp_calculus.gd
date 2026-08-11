## The display calculus: falsifiable properties of the interpolation engine.
##
## Each test asserts one property over a matrix of trajectory x link preset. The
## engine is driven as a pure display function through [NetwInterpHarness], so a
## failure names a broken display invariant, not a flaky scene. These properties
## pin today's buffered behavior before forecast lands, replacing the behavioral
## cases in the old interpolator suite.
class_name TestNetwInterpCalculus
extends NetwTestSuite

const _DURATION := 5.0
const _SEED := 1234

# Continuity slack over the raw per-frame motion. The smoothing catch-up and the
# dilation recovery can push a single step above the raw v*dt, bounded well under
# a gross teleport. Degraded links dilate harder, so their slack is looser while
# still catching a real teleport, which is orders of magnitude larger.
const _STEP_SLACK := 2.5
const _STEP_SLACK_DEGRADED := 3.5

# Jerk allowance over the raw per-frame motion, added to the trajectory's own
# curvature jerk. It bounds the second difference the dilation catch-up injects.
const _JERK_SLACK := 3.5

# Forecast projection error bound slack over the analytic tangent term, and a
# floor in units. The chord velocity and the fractional age both add to the pure
# curvature*age^2/2 deviation, so a real curve stays under a scaled bound.
const _FORECAST_SLACK := 1.5
const _FORECAST_EPS := 0.75

# The gapped, lossless link the forecast cells run on. Zero latency puts the
# forecast playhead on the live tick, so the displayed value is the projection
# error against live truth. The send gap forces a real projection every frame.
const _FORECAST_SEND_PERIOD := 3


class Cell:
	extends RefCounted

	var label: String
	var oracle: NetwInterpOracle
	var preset: NetwInterpDelivery.Preset


	func _init(p_label: String, p_oracle: NetwInterpOracle, p_preset: NetwInterpDelivery.Preset) -> void:
		label = p_label
		oracle = p_oracle
		preset = p_preset


func _linear() -> NetwInterpOracle:
	return NetwInterpOracle.linear(Vector2.ZERO, Vector2(120.0, 0.0))


func _circle() -> NetwInterpOracle:
	return NetwInterpOracle.circle(Vector2.ZERO, 100.0, 2.0)


# The stable P1/P2/P4 matrix: both trajectories over the clean-to-moderate links.
func _motion_cells() -> Array[Cell]:
	var cells: Array[Cell] = []
	for preset in [
		NetwInterpDelivery.perfect(),
		NetwInterpDelivery.wifi(),
		NetwInterpDelivery.mobile_4g(),
	]:
		cells.append(Cell.new("linear", _linear(), preset))
		cells.append(Cell.new("circle", _circle(), preset))
	return cells


# Lag is only meaningful for constant-velocity truth, so P3 runs linear only,
# over every link so the degraded lag floor is bounded too.
func _lag_cells() -> Array[Cell]:
	var cells: Array[Cell] = []
	for preset in NetwInterpDelivery.all():
		cells.append(Cell.new("linear", _linear(), preset))
	return cells


# Both trajectories over every link, clean to satellite, for the jerk bound.
func _all_cells() -> Array[Cell]:
	var cells: Array[Cell] = []
	for preset in NetwInterpDelivery.all():
		cells.append(Cell.new("linear", _linear(), preset))
		cells.append(Cell.new("circle", _circle(), preset))
	return cells


# The degraded links plus an explicit heavy-loss profile, for loss robustness.
func _degraded_cells() -> Array[Cell]:
	var cells: Array[Cell] = []
	var presets: Array[NetwInterpDelivery.Preset] = [
		NetwInterpDelivery.poor_3g(),
		NetwInterpDelivery.satellite(),
		NetwInterpDelivery.Preset.new(&"loss20", 40.0, 10.0, 0.20, 0.02),
	]
	for preset in presets:
		cells.append(Cell.new("linear", _linear(), preset))
		cells.append(Cell.new("circle", _circle(), preset))
	return cells


func _jerk_bound(h: NetwInterpHarness, oracle: NetwInterpOracle) -> float:
	var dt := h.frame_dt()
	return oracle.accel_max() * dt * dt + _JERK_SLACK * oracle.speed_max() * dt


func _run(cell: Cell) -> NetwInterpHarness:
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.send_period = 1
	h.configure(
		NetwInterpolate.new().lerp().smooth(0.05).to(&"value"),
		cell.oracle.value_at(0.0),
	)
	h.run(cell.oracle, cell.preset, _DURATION, _SEED)
	return h


func _run_chase(oracle: NetwInterpOracle) -> NetwInterpHarness:
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.configure_chase(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"value"),
		0.05,
		oracle.value_at(0.0),
	)
	h.run_chase(oracle, _DURATION)
	return h


func _lag_max_sec(h: NetwInterpHarness) -> float:
	return float(h.display_offset) / h.tickrate + 0.30


# P1 Monotonic display time. The playhead never moves backward.
func test_p1_monotonic_display_time() -> void:
	for cell in _motion_cells():
		var h := _run(cell)
		var m := NetwInterpMetrics.analyze(h, cell.oracle, h.warmup_hint())
		assert_bool(m.playhead_monotonic) \
				.override_failure_message(
					"P1 %s/%s: playhead ran backward by %.4f ticks" % [
						cell.label,
						cell.preset.name,
						m.max_playhead_backstep,
					],
				).is_true()
		assert_int(m.samples).is_greater(0)


# P2 Continuity. No per-frame step exceeds the motion bound. No invisible teleports.
func test_p2_continuity() -> void:
	for cell in _motion_cells():
		var h := _run(cell)
		var m := NetwInterpMetrics.analyze(h, cell.oracle, h.warmup_hint())
		var bound := cell.oracle.speed_max() * h.frame_dt() * _STEP_SLACK
		assert_float(m.max_step) \
				.override_failure_message(
					"P2 %s/%s: max step %.3f exceeds bound %.3f" % [
						cell.label,
						cell.preset.name,
						m.max_step,
						bound,
					],
				).is_less_equal(bound)


# P3 Bounded lag. The display trails the truth by a bounded, non-negative delay.
func test_p3_bounded_lag() -> void:
	for cell in _lag_cells():
		var h := _run(cell)
		var m := NetwInterpMetrics.analyze(h, cell.oracle, h.warmup_hint())
		var lag_max := _lag_max_sec(h)
		assert_bool(m.led_truth) \
				.override_failure_message(
					"P3 %s/%s: display led the truth (min lag %.3f s)" % [
						cell.label,
						cell.preset.name,
						m.min_lag_sec,
					],
				).is_false()
		assert_float(m.max_lag_sec) \
				.override_failure_message(
					"P3 %s/%s: lag %.3f s exceeds max %.3f s" % [
						cell.label,
						cell.preset.name,
						m.max_lag_sec,
						lag_max,
					],
				).is_less_equal(lag_max)
		assert_float(m.min_lag_sec).is_greater_equal(-0.01)


# P4 Steadiness. Constant-velocity-family truth yields steady forward motion:
# no stalls, no regressions, displayed speed converging to the truth speed.
func test_p4_steadiness() -> void:
	for cell in _motion_cells():
		var h := _run(cell)
		var m := NetwInterpMetrics.analyze(h, cell.oracle, h.warmup_hint())
		assert_int(m.regressions) \
				.override_failure_message(
					"P4 %s/%s: %d backward frames after warmup" % [
						cell.label,
						cell.preset.name,
						m.regressions,
					],
				).is_equal(0)
		assert_int(m.stalls) \
				.override_failure_message(
					"P4 %s/%s: %d stalled frames while truth moved" % [
						cell.label,
						cell.preset.name,
						m.stalls,
					],
				).is_equal(0)
		assert_int(m.moved).is_greater(0)
		var speed_err := absf(m.mean_speed - cell.oracle.speed_max()) \
				/ cell.oracle.speed_max()
		assert_float(speed_err) \
				.override_failure_message(
					"P4 %s/%s: mean speed %.2f vs truth %.2f (%.1f%% off)" % [
						cell.label,
						cell.preset.name,
						m.mean_speed,
						cell.oracle.speed_max(),
						speed_err * 100.0,
					],
				).is_less(0.05)


# P5 Bounded jerk. The second difference of the displayed sequence stays under a
# curvature-plus-dilation bound on every link. No visual hiccups, as one number.
func test_p5_bounded_jerk() -> void:
	for cell in _all_cells():
		var h := _run(cell)
		var m := NetwInterpMetrics.analyze(h, cell.oracle, h.warmup_hint())
		var bound := _jerk_bound(h, cell.oracle)
		assert_float(m.max_jerk) \
				.override_failure_message(
					"P5 %s/%s: jerk %.3f exceeds bound %.3f" % [
						cell.label,
						cell.preset.name,
						m.max_jerk,
						bound,
					],
				).is_less_equal(bound)


# P5 for the predicted CHASE path. The chase filter must smooth a body that
# snaps once per tick into a hiccup-free display. This is the open v2 sub-tick
# jitter question answered as a green test instead of a memory note.
func test_p5_chase_bounded_jerk() -> void:
	for oracle in [_linear(), _circle()]:
		var h := _run_chase(oracle)
		var m := NetwInterpMetrics.analyze(h, oracle, 0.5)
		var bound := _jerk_bound(h, oracle)
		assert_bool(m.playhead_monotonic).is_true()
		assert_int(m.regressions) \
				.override_failure_message(
					"P5 chase/%s: %d backward frames" % [
						oracle.kind,
						m.regressions,
					],
				).is_equal(0)
		assert_float(m.max_jerk) \
				.override_failure_message(
					"P5 chase/%s: jerk %.3f exceeds bound %.3f" % [
						oracle.kind,
						m.max_jerk,
						bound,
					],
				).is_less_equal(bound)


# A chase harness settled at zero with near-exact tracking, the rig every
# recovery-absorption law starts from.
func _settled_chase() -> NetwInterpHarness:
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.configure_chase(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"value"),
		0.005,
		0.0,
	)
	h.set_body(0.0)
	for f in 30:
		h.step_chase(float(f) / h.fps)
	return h


# P7 Recovery absorption. A recovery moves the body in one write; the chase
# absorbs the jump as a decaying render offset, so the display stays put at
# the correction and glides onto the corrected body with no residue.
func test_p7_chase_absorbs_a_recovery_without_a_jump() -> void:
	var h := _settled_chase()

	h.set_body(1.0)
	h.absorb_recovery({ &"value": 1.0 })
	h.step_chase(0.6)
	assert_float(absf(float(h.displayed.back()))) \
			.override_failure_message(
				"the display must stay near the pre-correction pose, not jump "
				+ "with the body",
			).is_less(0.3)

	for f in 90:
		h.step_chase(0.7 + float(f) / h.fps)
	assert_float(float(h.displayed.back())).override_failure_message(
		"the offset must decay to nothing, leaving the display on the "
		+ "corrected body",
	).is_equal_approx(1.0, 0.02)


# P7 A correction TRAIN is absorbed as smoothly as a single correction.
#
# One recovery has nothing outstanding to lose, so any absorber looks right on
# it. A train is where the two candidate rules separate. Absorbing onto the
# offset still gliding keeps the display continuous, while replacing it hands
# the screen whatever had not decayed yet, once per correction. That residual is
# most of the write when corrections arrive every few frames, which is a step
# per correction rather than the one glide the absorption promises.
func test_p7_chase_absorbs_a_correction_train_without_stepping() -> void:
	var h := _settled_chase()
	var body := 0.0
	var last := float(h.displayed.back())
	var worst_step := 0.0
	# A correction every third frame, the rate sustained contact produces.
	for f in 60:
		if f % 3 == 0:
			body += 0.1
			h.set_body(body)
			h.absorb_recovery({ &"value": 0.1 })
		h.step_chase(0.6 + float(f) / h.fps)
		var shown := float(h.displayed.back())
		worst_step = maxf(worst_step, absf(shown - last))
		last = shown

	# The body advances 0.1 per correction. A display that steps by about that
	# much is showing the correction rather than absorbing it.
	assert_float(worst_step).override_failure_message(
		(
				"the display stepped %.4f in one frame against a 0.1 correction, "
				+ "so the train is reaching the screen instead of being glided off"
		) % worst_step,
	).is_less(0.05)


# A teleported recovery clears every offset: a genuine desync should be seen
# to snap, never smoothed through.
func test_p7_a_teleport_snaps_the_chase() -> void:
	var h := _settled_chase()

	h.set_body(5.0)
	h.absorb_recovery({ &"value": 5.0 }, true)
	h.step_chase(0.6)

	assert_float(float(h.displayed.back())).override_failure_message(
		"a teleported recovery must snap the display with the body",
	).is_greater(4.5)


# The absorbed offset is clamped by magnitude with its direction preserved, so
# a huge correction can never wind the visual further from the body than a
# teleport would have moved it.
func test_p7_the_chase_offset_clamps_by_magnitude() -> void:
	var clamped: Vector3 = DisplayCore._clamp_delta(
		Vector3(10.0, 0.0, 0.0),
		2.0,
	)
	assert_float(clamped.length()).is_equal_approx(2.0, 0.0001)
	assert_float(clamped.x).is_greater(0.0)
	assert_float(float(DisplayCore._clamp_delta(-9.0, 2.0))) \
			.is_equal_approx(-2.0, 0.0001)


func test_p8_role_offsets_preserve_shortest_rotation_channels() -> void:
	var displayed_angle := deg_to_rad(179.0)
	var target_angle := deg_to_rad(-179.0)
	var angle_offset: float = DisplayCore._role_offset(
		displayed_angle,
		target_angle,
		NetwInterpolate.MODE_ANGLE,
	)
	assert_float(absf(angle_offset)).is_equal_approx(deg_to_rad(2.0), 0.0001)

	var displayed := Quaternion(Vector3.UP, deg_to_rad(170.0))
	var target := Quaternion(Vector3.UP, deg_to_rad(-170.0))
	var offset: Quaternion = DisplayCore._role_offset(
		displayed,
		target,
		NetwInterpolate.MODE_SLERP,
	)
	var recomposed: Quaternion = DisplayCore._add_delta(
		target,
		offset,
	)
	assert_float(recomposed.angle_to(displayed)).is_less(0.0001)


# P6 Loss robustness. Under heavy loss and jitter the display still holds P1, P2,
# and P4: monotonic, continuous, no regressions, no stalls. Starvation grows lag,
# it never tears the output. The bounded-lag check rides P3's degraded floor.
func test_p6_loss_robustness() -> void:
	for cell in _degraded_cells():
		var h := _run(cell)
		var m := NetwInterpMetrics.analyze(h, cell.oracle, h.warmup_hint())
		var bound := cell.oracle.speed_max() * h.frame_dt() * _STEP_SLACK_DEGRADED
		assert_bool(m.playhead_monotonic) \
				.override_failure_message(
					"P6 %s/%s: playhead ran backward by %.4f" % [
						cell.label,
						cell.preset.name,
						m.max_playhead_backstep,
					],
				).is_true()
		assert_int(m.regressions) \
				.override_failure_message(
					"P6 %s/%s: %d backward frames under loss" % [
						cell.label,
						cell.preset.name,
						m.regressions,
					],
				).is_equal(0)
		assert_int(m.stalls) \
				.override_failure_message(
					"P6 %s/%s: %d stalled frames under loss" % [
						cell.label,
						cell.preset.name,
						m.stalls,
					],
				).is_equal(0)
		assert_float(m.max_step) \
				.override_failure_message(
					"P6 %s/%s: max step %.3f exceeds bound %.3f" % [
						cell.label,
						cell.preset.name,
						m.max_step,
						bound,
					],
				).is_less_equal(bound)


func _first_diff(a: Array, b: Array) -> int:
	if a.size() != b.size():
		return -2
	for i in a.size():
		if a[i] != b[i]:
			return i
	return -1


func _stats_equal(a: NetwPumpStats, b: NetwPumpStats) -> bool:
	return a.runtimes == b.runtimes \
			and a.starving == b.starving \
			and a.sleeping == b.sleeping \
			and a.projecting == b.projecting \
			and a.snaps == b.snaps \
			and is_equal_approx(a.max_display_lag, b.max_display_lag) \
			and is_equal_approx(a.max_forecast_age, b.max_forecast_age)


# P9 Replay determinism. Two runs of the same scenario emit identical displayed
# sequences and identical pump stats. No wall clock, frame counter, or iteration
# order leaks into the output.
func test_p9_replay_determinism() -> void:
	var cell := Cell.new("circle", _circle(), NetwInterpDelivery.poor_3g())
	var a := _run(cell)
	var b := _run(cell)
	var diff := _first_diff(a.displayed, b.displayed)
	assert_int(diff) \
			.override_failure_message(
				"P9: displayed sequences diverge at frame %d" % diff,
			).is_equal(-1)
	assert_int(_first_diff(a.writer().samples, b.writer().samples)).is_equal(-1)
	assert_bool(_stats_equal(a.run_stats, b.run_stats)) \
			.override_failure_message("P9: pump stats differ between runs") \
			.is_true()


# P10 Schedule independence. Two engines fed identical streams but pumping their
# runtimes in permuted order emit identical per-lane sequences and identical
# merged stats. Runtimes share no state, so any partition of the pump preserves
# semantics. This is the property the native fork-join leans on.
func test_p10_schedule_independence() -> void:
	var lanes: Array = [
		[_linear(), NetwInterpDelivery.wifi(), 11],
		[_circle(), NetwInterpDelivery.poor_3g(), 22],
		[_linear(), NetwInterpDelivery.mobile_4g(), 33],
	]
	var forward := schedule(lanes, PackedInt32Array([0, 1, 2]))
	var permuted := schedule(lanes, PackedInt32Array([2, 0, 1]))
	for i in lanes.size():
		var diff := _first_diff(forward.lane(i).displayed, permuted.lane(i).displayed)
		assert_int(diff) \
				.override_failure_message(
					"P10: lane %d diverges under permuted pump order at frame %d" % [
						i,
						diff,
					],
				).is_equal(-1)
	assert_bool(_stats_equal(forward.run_stats, permuted.run_stats)) \
			.override_failure_message("P10: merged stats depend on pump order") \
			.is_true()


func schedule(lanes: Array, order: PackedInt32Array) -> NetwInterpScheduleHarness:
	var s := NetwInterpScheduleHarness.new()
	s.tickrate = 60.0
	s.fps = 60.0
	s.send_period = 1
	for spec in lanes:
		s.add_lane(spec[0], spec[1], spec[2])
	s.run(_DURATION, order)
	return s


# A single-channel forecast display over the gapped lossless link, finite-
# difference projection with no smoothing so the displayed value is the raw
# projection. The playhead sits on the live tick, so display error is projection
# error against live truth.
func _run_forecast(oracle: NetwInterpOracle, smoothing: float) -> NetwInterpHarness:
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.send_period = _FORECAST_SEND_PERIOD
	h.timeline_mode = NetwDisplayHandle.TimelineMode.FORECAST
	h.configure(
		NetwInterpolate.new().lerp().smooth(smoothing).to(&"value"),
		oracle.value_at(0.0),
	)
	h.run(oracle, NetwInterpDelivery.perfect(), _DURATION, _SEED)
	return h


# The same link and cadence as [method _run_forecast] but buffered, so the two
# differ only in the timeline mode the leads property compares.
func _run_buffered(oracle: NetwInterpOracle, smoothing: float) -> NetwInterpHarness:
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.send_period = _FORECAST_SEND_PERIOD
	h.configure(
		NetwInterpolate.new().lerp().smooth(smoothing).to(&"value"),
		oracle.value_at(0.0),
	)
	h.run(oracle, NetwInterpDelivery.perfect(), _DURATION, _SEED)
	return h


# Largest displayed distance from the truth at the playhead time, past warmup.
# Under a zero-latency link the playhead time is the live tick.
func _max_error_at_playhead(h: NetwInterpHarness, oracle: NetwInterpOracle) -> float:
	var warm := h.warmup_hint()
	var worst := 0.0
	for i in h.frames.size():
		if h.frames[i] < warm:
			continue
		var t := h.playhead_time[i] / h.tickrate
		var disp: Vector2 = h.displayed[i]
		worst = maxf(worst, disp.distance_to(oracle.value_at(t)))
	return worst


# Mean displayed distance from live truth at the frame's wall time, past warmup.
func _mean_error_vs_live(h: NetwInterpHarness, oracle: NetwInterpOracle) -> float:
	var warm := h.warmup_hint()
	var total := 0.0
	var count := 0
	for i in h.frames.size():
		if h.frames[i] < warm:
			continue
		var disp: Vector2 = h.displayed[i]
		total += disp.distance_to(oracle.value_at(h.frames[i]))
		count += 1
	return total / maxf(1.0, float(count))


# P7 Forecast error bound. Under FORECAST the displayed value projects past the
# newest sample and stays within the tangent bound: straight lines forecast
# exactly, curves within curvature*age^2. The projection respects the tick cap.
func test_p7_forecast_error_bounded() -> void:
	for oracle: NetwInterpOracle in [_linear(), _circle()]:
		var h := _run_forecast(oracle, 0.0)
		assert_int(h.run_stats.projecting) \
				.override_failure_message(
					"P7 %s: forecast never projected" % oracle.kind,
				).is_greater(0)
		assert_float(h.run_stats.max_forecast_age) \
				.override_failure_message(
					"P7 %s: projected %.2f ticks past the %d-tick cap" % [
						oracle.kind,
						h.run_stats.max_forecast_age,
						h.max_forecast_ticks,
					],
				).is_less_equal(float(h.max_forecast_ticks) + 0.001)

		var age_sec := h.run_stats.max_forecast_age / h.tickrate
		var span_sec := float(h.send_period) / h.tickrate
		var bound := oracle.accel_max() * age_sec * (age_sec + span_sec) \
				* _FORECAST_SLACK + _FORECAST_EPS
		var err := _max_error_at_playhead(h, oracle)
		assert_float(err) \
				.override_failure_message(
					"P7 %s: projection error %.3f exceeds bound %.3f" % [
						oracle.kind,
						err,
						bound,
					],
				).is_less_equal(bound)


# P7 Forecast leads the buffer. The same stream displayed under FORECAST tracks
# live truth more closely than under BUFFERED, and does so without tearing:
# monotonic playhead, no backward frames.
func test_p7_forecast_leads_buffered() -> void:
	for oracle: NetwInterpOracle in [_linear(), _circle()]:
		var f := _run_forecast(oracle, 0.05)
		var b := _run_buffered(oracle, 0.05)
		var f_err := _mean_error_vs_live(f, oracle)
		var b_err := _mean_error_vs_live(b, oracle)
		assert_float(f_err) \
				.override_failure_message(
					"P7 %s: forecast error %.3f not below buffered %.3f" % [
						oracle.kind,
						f_err,
						b_err,
					],
				).is_less(b_err)
		var m := NetwInterpMetrics.analyze(f, oracle, f.warmup_hint())
		assert_bool(m.playhead_monotonic) \
				.override_failure_message(
					"P7 %s: forecast playhead ran backward by %.4f" % [
						oracle.kind,
						m.max_playhead_backstep,
					],
				).is_true()
		assert_int(m.regressions) \
				.override_failure_message(
					"P7 %s: %d backward frames under forecast" % [
						oracle.kind,
						m.regressions,
					],
				).is_equal(0)


# The server never forecasts. A BRACKETED runtime, the authored and server path,
# holds its newest sample even with the forecast handle set. Structural, since
# only the REMOTE branch reads the timeline mode.
func test_p7_server_never_forecasts() -> void:
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.send_period = _FORECAST_SEND_PERIOD
	h.timeline_mode = NetwDisplayHandle.TimelineMode.FORECAST
	h.pump_mode = DisplayCore._PUMP_BRACKETED
	h.configure(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"value"),
		_linear().value_at(0.0),
	)
	h.run(_linear(), NetwInterpDelivery.perfect(), _DURATION, _SEED)
	assert_int(h.run_stats.projecting) \
			.override_failure_message("P7: a bracketed server runtime projected") \
			.is_equal(0)


# P8 Sleep correctness. A stream that stops while truth is stationary sleeps and
# never projects a still value. A stream that stops while moving keeps projecting
# to the cap, then holds instead of drifting away.
func test_p8_stationary_stream_sleeps() -> void:
	var oracle := NetwInterpOracle.stationary(Vector2(10.0, 5.0))
	var h := _run_forecast(oracle, 0.0)
	assert_int(h.run_stats.sleeping) \
			.override_failure_message("P8: a stopped stationary stream never slept") \
			.is_greater(0)
	assert_int(h.run_stats.projecting) \
			.override_failure_message("P8: a stationary stream projected a still value") \
			.is_equal(0)
	var warm := h.warmup_hint()
	for i in h.frames.size():
		if h.frames[i] < warm:
			continue
		var disp: Vector2 = h.displayed[i]
		assert_float(disp.distance_to(oracle.origin)) \
				.override_failure_message("P8: a stationary display drifted") \
				.is_less(0.001)


func test_p8_moving_stream_projects_to_cap_then_holds() -> void:
	var oracle := _linear()
	var h := NetwInterpHarness.new()
	h.tickrate = 60.0
	h.fps = 60.0
	h.send_period = 1
	h.timeline_mode = NetwDisplayHandle.TimelineMode.FORECAST
	# The stream falls silent after one second while the truth keeps moving.
	h.record_cutoff_sec = 1.0
	h.configure(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"value"),
		oracle.value_at(0.0),
	)
	h.run(oracle, NetwInterpDelivery.perfect(), 3.0, _SEED)

	assert_int(h.run_stats.projecting) \
			.override_failure_message("P8: a stalled moving stream never projected") \
			.is_greater(0)
	# The projection ran out to the cap once the stream stopped feeding.
	assert_float(h.run_stats.max_forecast_age) \
			.override_failure_message(
				"P8: projected only %.2f ticks, never reached the %d-tick cap" % [
					h.run_stats.max_forecast_age,
					h.max_forecast_ticks,
				],
			).is_greater_equal(float(h.max_forecast_ticks) - 1.0)
	assert_float(h.run_stats.max_forecast_age) \
			.is_less_equal(float(h.max_forecast_ticks) + 0.001)

	# Past the cap the display holds: the last two frames sit on the same point.
	var n := h.displayed.size()
	var last: Vector2 = h.displayed[n - 1]
	var prev: Vector2 = h.displayed[n - 2]
	assert_float(last.distance_to(prev)) \
			.override_failure_message("P8: the display kept drifting past the cap") \
			.is_less(0.01)
