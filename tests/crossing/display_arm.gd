## The display family's Bridge-A arm: drives the interpolation history through
## scripted trajectories and compares what it sampled to the committed golden.
##
## This is the instrument, not a test. The history is a pure resampling function
## with no seam and no byte grammar, so the only thing that can license deleting
## its GDScript arm is a trace recorded from that arm BEFORE its replacement
## existed. Recording it afterwards would describe what the port produced rather
## than what it must reproduce, and no amount of later work recovers that. The
## golden below was recorded at c4f11bda, when the history was still a
## GDScript class, and the arm now drives [NetwDisplayHistory] against it.
##
## [br][br]
## Every scenario drives [NetwDisplayHistory] alone, with no runtime, no clock
## and no node, so the whole trace is a function of the calls the arm makes.
## Sample values are the observation, and the sleep, projection and snap flags
## are read alongside them because the pump reads those flags into its stats
## rather than recomputing what the history already decided.
## [codeblock]
## godot --headless --path . -s res://tests/crossing/display_arm.gd
## godot --headless --path . -s res://tests/crossing/display_arm.gd -- --record
## [/codeblock]
##
## Exits 1 on the first differing row, naming the scenario and both sides.
extends SceneTree

const GOLDEN := "res://tests/native/goldens/display_history.txt"

## Digits a continuous reading is written to. Every sampled value is a lerp of
## authored endpoints, so the inputs below are chosen to land far from a
## rounding boundary at this width.
const PRECISION := 6

## The tick period every projecting scenario advances by, exactly representable
## in binary so an age multiplied by it is exact rather than close.
const TICKTIME := 0.0625

const SCENARIOS: Array[StringName] = [
	&"record/a_repeated_value_is_not_recorded_twice",
	&"record/clearing_empties_the_history_and_frees_the_domain",
	&"bracket/an_authoring_stream_keys_and_brackets_by_authoring_ticks",
	&"sample/before_the_first_tick_the_last_written_value_holds",
	&"sample/an_interior_playhead_lerps_across_its_bracket",
	&"sample/a_gap_wider_than_twice_the_interval_holds_then_lerps",
	&"sample/a_bracket_wider_than_the_snap_distance_jumps",
	&"tail/a_settled_channel_holds_and_sleeps",
	&"tail/a_moved_channel_holds_awake",
	&"forecast/the_tail_projects_by_finite_difference",
	&"forecast/an_explicit_derivative_replaces_the_finite_difference",
	&"forecast/the_age_is_capped_by_the_forecast_budget",
	&"forecast/a_negligible_velocity_holds_instead_of_projecting",
	&"forecast/an_unprojectable_type_holds",
	&"smooth/a_partial_weight_blends_toward_the_sample",
	&"smooth/a_full_weight_takes_the_sample_whole",
	&"smooth/a_distance_past_the_snap_takes_the_sample_whole",
	&"mode/angle_lerps_the_short_way_around",
	&"mode/slerp_walks_the_arc_between_two_rotations",
]

var _rows: Array[String] = []


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
	for scenario in SCENARIOS:
		match scenario:
			&"record/a_repeated_value_is_not_recorded_twice":
				_a_repeated_value_is_not_recorded_twice(scenario)
			&"record/clearing_empties_the_history_and_frees_the_domain":
				_clearing_empties_the_history_and_frees_the_domain(scenario)
			&"bracket/an_authoring_stream_keys_and_brackets_by_authoring_ticks":
				_an_authoring_stream_keys_and_brackets_by_authoring_ticks(scenario)
			&"sample/before_the_first_tick_the_last_written_value_holds":
				_before_the_first_tick_the_last_written_value_holds(scenario)
			&"sample/an_interior_playhead_lerps_across_its_bracket":
				_an_interior_playhead_lerps_across_its_bracket(scenario)
			&"sample/a_gap_wider_than_twice_the_interval_holds_then_lerps":
				_a_gap_wider_than_twice_the_interval_holds_then_lerps(scenario)
			&"sample/a_bracket_wider_than_the_snap_distance_jumps":
				_a_bracket_wider_than_the_snap_distance_jumps(scenario)
			&"tail/a_settled_channel_holds_and_sleeps":
				_a_settled_channel_holds_and_sleeps(scenario)
			&"tail/a_moved_channel_holds_awake":
				_a_moved_channel_holds_awake(scenario)
			&"forecast/the_tail_projects_by_finite_difference":
				_the_tail_projects_by_finite_difference(scenario)
			&"forecast/an_explicit_derivative_replaces_the_finite_difference":
				_an_explicit_derivative_replaces_the_finite_difference(scenario)
			&"forecast/the_age_is_capped_by_the_forecast_budget":
				_the_age_is_capped_by_the_forecast_budget(scenario)
			&"forecast/a_negligible_velocity_holds_instead_of_projecting":
				_a_negligible_velocity_holds_instead_of_projecting(scenario)
			&"forecast/an_unprojectable_type_holds":
				_an_unprojectable_type_holds(scenario)
			&"smooth/a_partial_weight_blends_toward_the_sample":
				_a_partial_weight_blends_toward_the_sample(scenario)
			&"smooth/a_full_weight_takes_the_sample_whole":
				_a_full_weight_takes_the_sample_whole(scenario)
			&"smooth/a_distance_past_the_snap_takes_the_sample_whole":
				_a_distance_past_the_snap_takes_the_sample_whole(scenario)
			&"mode/angle_lerps_the_short_way_around":
				_angle_lerps_the_short_way_around(scenario)
			&"mode/slerp_walks_the_arc_between_two_rotations":
				_slerp_walks_the_arc_between_two_rotations(scenario)
			_:
				printerr("unknown scenario: %s" % scenario)
	return _rows


#region Recording

# A stream that keeps resending a settled value must not grow the buffer, or a
# quiet channel would never let another writer take over.
func _a_repeated_value_is_not_recorded_twice(scenario: StringName) -> void:
	var history := _history()
	history.record(4, Vector2(2.0, 0.0), false)
	history.record(6, Vector2(2.0, 0.0), false)
	_row(scenario, "duplicate", { &"newest": history.newest_tick() })

	history.record(8, Vector2(3.0, 0.0), false)
	_row(scenario, "distinct", { &"newest": history.newest_tick() })


func _clearing_empties_the_history_and_frees_the_domain(
		scenario: StringName,
) -> void:
	var history := _history()
	history.record(4, Vector2(2.0, 0.0), false)
	history.clear()
	_row(scenario, "cleared", { &"empty": history.is_empty() })

	# The tick domain pins on the first record and a clear frees it, so the same
	# channel may be refed from the other domain after a teleport.
	history.record(40, Vector2(2.0, 0.0), true)
	_row(scenario, "refed", { &"newest": history.newest_tick() })

#endregion

#region Bracketing

func _an_authoring_stream_keys_and_brackets_by_authoring_ticks(
		scenario: StringName,
) -> void:
	var history := _history()
	history.record(100, Vector2(0.0, 0.0), true)
	history.record(104, Vector2(40.0, 0.0), true)
	for tick in [99, 100, 102, 104, 106]:
		var bracket: Vector2i = history.bracketing_ticks(tick)
		_row(
			scenario,
			"bracket",
			{
				&"at": tick,
				&"prev": bracket.x,
				&"next": bracket.y,
				&"after": history.has_tick_after(tick),
			},
		)

#endregion

#region Sampling

func _before_the_first_tick_the_last_written_value_holds(
		scenario: StringName,
) -> void:
	var history := _history()
	history.record(10, Vector2(4.0, 0.0), false)
	_sampled(scenario, history, 6, 0.0, Vector2(-1.0, -2.0), 3)


func _an_interior_playhead_lerps_across_its_bracket(
		scenario: StringName,
) -> void:
	var history := _history()
	history.record(10, Vector2(0.0, 0.0), false)
	history.record(14, Vector2(8.0, 4.0), false)
	for step in [0.0, 0.25, 0.5, 0.75]:
		_sampled(scenario, history, 12, step, Vector2.ZERO, 3)


# A gap wider than twice the expected interval is a hole in the stream rather
# than a slow channel, so the playhead holds the older sample and spends only the
# last interval crossing to the newer one.
#
# The three gaps below straddle the threshold rather than clearing it, because a
# scenario that only ever takes the wide branch passes just as well against a
# threshold set anywhere below its gap. Six is twice the interval and takes the
# ordinary branch, which is what pins the comparison as strict.
func _a_gap_wider_than_twice_the_interval_holds_then_lerps(
		scenario: StringName,
) -> void:
	var wide := _history()
	wide.record(0, Vector2(0.0, 0.0), false)
	wide.record(20, Vector2(20.0, 0.0), false)
	for tick in [4, 12, 16, 18, 19]:
		_sampled(scenario, wide, tick, 0.0, Vector2.ZERO, 3)

	var near := _history()
	near.record(0, Vector2(0.0, 0.0), false)
	near.record(7, Vector2(14.0, 0.0), false)
	for tick in [2, 4, 5]:
		_sampled(scenario, near, tick, 0.0, Vector2.ZERO, 3)

	var at_threshold := _history()
	at_threshold.record(0, Vector2(0.0, 0.0), false)
	at_threshold.record(6, Vector2(12.0, 0.0), false)
	for tick in [1, 3]:
		_sampled(scenario, at_threshold, tick, 0.0, Vector2.ZERO, 3)


func _a_bracket_wider_than_the_snap_distance_jumps(
		scenario: StringName,
) -> void:
	var history := _history()
	history.snap_distance = 5.0
	history.record(10, Vector2(0.0, 0.0), false)
	history.record(12, Vector2(40.0, 0.0), false)
	_sampled(scenario, history, 10, 0.5, Vector2.ZERO, 3)

	# A bracket inside the snap distance interpolates as usual.
	var near := _history()
	near.snap_distance = 5.0
	near.record(10, Vector2(0.0, 0.0), false)
	near.record(12, Vector2(4.0, 0.0), false)
	_sampled(scenario, near, 10, 0.5, Vector2.ZERO, 3)

#endregion

#region The buffered tail

# A channel whose newest sample is already displayed has nothing left to show, so
# it sleeps and the pump defers to whatever else writes the property.
func _a_settled_channel_holds_and_sleeps(scenario: StringName) -> void:
	var history := _history()
	var settled := Vector2(5.0, 0.0)
	history.record(0, settled, false)
	_sampled(scenario, history, 4, 0.0, settled, 3)

	# A repeat of the settled value is not a revival.
	history.record(1, settled, false)
	_row(scenario, "after_duplicate", { &"sleeping": history.sleeping })

	# A distinct record is, and it wakes the channel without a sample.
	history.record(6, Vector2(50.0, 0.0), false)
	_row(scenario, "after_distinct", { &"sleeping": history.sleeping })


func _a_moved_channel_holds_awake(scenario: StringName) -> void:
	var history := _history()
	history.record(0, Vector2(5.0, 0.0), false)
	_sampled(scenario, history, 4, 0.0, Vector2(-30.0, 0.0), 3)

#endregion

#region The forecasting tail

func _the_tail_projects_by_finite_difference(scenario: StringName) -> void:
	var history := _history()
	history.record(0, Vector2(0.0, 0.0), false)
	history.record(4, Vector2(8.0, 0.0), false)
	for step in [0.0, 0.5]:
		_sampled(scenario, history, 6, step, Vector2.ZERO, 3, true, 8, TICKTIME)


# The sibling derivative is read at the channel's own newest tick, an atomic
# pair, so a torn pair can never manufacture a phantom trajectory.
func _an_explicit_derivative_replaces_the_finite_difference(
		scenario: StringName,
) -> void:
	var history := _history()
	history.record(0, Vector2(0.0, 0.0), false)
	history.record(4, Vector2(8.0, 0.0), false)
	_sampled(
		scenario,
		history,
		6,
		0.0,
		Vector2.ZERO,
		3,
		true,
		8,
		TICKTIME,
		Vector2(0.0, 64.0),
		true,
	)


func _the_age_is_capped_by_the_forecast_budget(scenario: StringName) -> void:
	var history := _history()
	history.record(0, Vector2(0.0, 0.0), false)
	history.record(4, Vector2(8.0, 0.0), false)
	for budget in [0, 2, 8]:
		_sampled(
			scenario,
			history,
			12,
			0.0,
			Vector2.ZERO,
			3,
			true,
			budget,
			TICKTIME,
		)


# A projection too small to move the display is worth less than the sleep it
# forfeits, so a stationary channel falls through to the buffered hold.
func _a_negligible_velocity_holds_instead_of_projecting(
		scenario: StringName,
) -> void:
	var history := _history()
	var settled := Vector2(5.0, 0.0)
	history.record(0, settled, false)
	_sampled(scenario, history, 6, 0.0, settled, 3, true, 8, TICKTIME)


func _an_unprojectable_type_holds(scenario: StringName) -> void:
	var history := _history()
	history.record(0, Color(0.25, 0.5, 0.75, 1.0), false)
	history.record(4, Color(0.75, 0.5, 0.25, 1.0), false)
	_sampled(
		scenario,
		history,
		6,
		0.0,
		Color(0.0, 0.0, 0.0, 1.0),
		3,
		true,
		8,
		TICKTIME,
	)

#endregion

#region Smoothing

func _a_partial_weight_blends_toward_the_sample(scenario: StringName) -> void:
	var history := _history()
	for weight in [0.25, 0.5]:
		_smoothed(scenario, history, Vector2(0.0, 0.0), Vector2(8.0, 4.0), weight)


func _a_full_weight_takes_the_sample_whole(scenario: StringName) -> void:
	var history := _history()
	_smoothed(scenario, history, Vector2(0.0, 0.0), Vector2(8.0, 4.0), 1.0)


func _a_distance_past_the_snap_takes_the_sample_whole(
		scenario: StringName,
) -> void:
	var history := _history()
	history.snap_distance = 5.0
	_smoothed(scenario, history, Vector2(0.0, 0.0), Vector2(40.0, 0.0), 0.25)
	_smoothed(scenario, history, Vector2(0.0, 0.0), Vector2(2.0, 0.0), 0.25)

#endregion

#region Modes

# The short way around is the whole point of the angle mode: a lerp of the raw
# radians would sweep the long way through every intermediate heading.
func _angle_lerps_the_short_way_around(scenario: StringName) -> void:
	var history := _history()
	history.mode = NetwInterpolate.MODE_ANGLE
	history.record(0, -3.0, false)
	history.record(4, 3.0, false)
	for step in [0.0, 0.5]:
		_sampled(scenario, history, 2, step, 0.0, 3)


# The rows below do not tell the two rotation modes apart, and no rows can:
# Godot's [method @GlobalScope.lerp] dispatches a [Quaternion] to the same
# spherical walk [method Quaternion.slerp] runs, so a quaternion channel takes
# the arc whichever mode it declares. What the rows pin is that arc.
func _slerp_walks_the_arc_between_two_rotations(scenario: StringName) -> void:
	var history := _history()
	history.mode = NetwInterpolate.MODE_SLERP
	history.record(0, Quaternion.IDENTITY, false)
	history.record(4, Quaternion(Vector3(0.0, 0.0, 1.0), PI * 0.5), false)
	for step in [0.0, 0.5]:
		_sampled(scenario, history, 2, step, Quaternion.IDENTITY, 3)

#endregion

#region Rig

func _history() -> NetwDisplayHistory:
	var history := NetwDisplayHistory.new()
	history.mode = NetwInterpolate.MODE_LERP
	return history


# One sample, with the flags the pump reads out of the history alongside it.
func _sampled(
		scenario: StringName,
		history: NetwDisplayHistory,
		dt: int,
		factor: float,
		last_written: Variant,
		expected_interval_ticks: int,
		forecast := false,
		max_forecast_ticks := 0,
		ticktime := 0.0,
		explicit_velocity: Variant = null,
		has_explicit_velocity := false,
) -> void:
	var value: Variant = history.sample(
		dt,
		factor,
		last_written,
		expected_interval_ticks,
		forecast,
		max_forecast_ticks,
		ticktime,
		explicit_velocity,
		has_explicit_velocity,
	)
	_row(
		scenario,
		"sample",
		{
			&"dt": dt,
			&"factor": factor,
			&"value": value,
			&"projected": history.has_projected(),
			&"age": history.get_project_age(),
			&"snapped": history.has_snapped(),
			&"sleeping": history.sleeping,
		},
	)


func _smoothed(
		scenario: StringName,
		history: NetwDisplayHistory,
		last_written: Variant,
		target: Variant,
		weight: float,
) -> void:
	var value: Variant = history.smooth_toward(last_written, target, weight)
	_row(
		scenario,
		"smooth",
		{
			&"weight": weight,
			&"value": value,
			&"snapped": history.has_snapped(),
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
	match typeof(value):
		TYPE_NIL:
			return "null"
		TYPE_BOOL:
			return "true" if value else "false"
		TYPE_FLOAT:
			return _real(value)
		TYPE_VECTOR2:
			return "(%s,%s)" % [_real(value.x), _real(value.y)]
		TYPE_VECTOR3:
			return "(%s,%s,%s)" % [_real(value.x), _real(value.y), _real(value.z)]
		TYPE_QUATERNION:
			return "(%s,%s,%s,%s)" % [
				_real(value.x),
				_real(value.y),
				_real(value.z),
				_real(value.w),
			]
		TYPE_COLOR:
			return "(%s,%s,%s,%s)" % [
				_real(value.r),
				_real(value.g),
				_real(value.b),
				_real(value.a),
			]
	return str(value)


# A continuous reading at the declared precision, padded so the width is the
# claim rather than a coincidence of the value, and with negative zero
# normalized so a sign that carries no information cannot move a row.
func _real(value: float) -> String:
	var text := "%.*f" % [PRECISION, value]
	return text.substr(1) if text.begins_with("-0.000000") else text


func _header() -> String:
	return (
		"# The display family's Bridge-A trace, recorded from the GDScript\n"
		+ "# interpolation history before any of it was native. Rows are\n"
		+ "# scenario|kind|key=value, keys sorted, continuous readings at 6\n"
		+ "# digits, tick-domain values exact.\n"
		+ "#\n"
		+ "# Regenerate with:\n"
		+ "#   godot --headless --path . -s res://tests/crossing/display_arm.gd \\\n"
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
