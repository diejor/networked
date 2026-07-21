## Laws for the four decisions the prediction engine makes, asserted directly.
##
## Every earlier law in this campaign had to stand a simulation up to ask what
## the engine would decide, which meant a wrong answer and a wrong body could
## cancel out. Fencing the decisions made them reachable on their own, so these
## laws pin the choice itself: which drive a frame applies, whether authority
## replays or holds, whether a transition diverged, and what a correction
## restores. No entity, no clock, no body.
class_name TestPredictKernels
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const DriveKind := PredictionHandle.DriveKind
const CorrectionMode := PredictionHandle.CorrectionMode
const RecoveryPolicy := PredictionHandle.RecoveryPolicy
const RestoreMode := PredictionHandle.RestoreMode
const ConsumeAction := Engine_.ConsumeAction
const ExactVerdict := Engine_.ExactVerdict
const Domain := NetwPredictJournal.Domain


# --- predict_fold ---


func test_a_frame_with_newer_input_drives_it_fresh() -> void:
	var plan := Engine_.predict_fold(7, 4, 99)
	assert_int(plan[&"label"]).is_equal(7)
	assert_bool(plan[&"fresh"]).is_true()
	assert_int(plan[&"kind"]).is_equal(DriveKind.FRESH)


# The FRAME tier drives once per frame whether or not a tick authored fresh
# input, so a frame with nothing newer repeats the command it already has under
# the label it already had. It never invents a label.
func test_a_frame_with_no_newer_input_repeats_the_same_label() -> void:
	var plan := Engine_.predict_fold(7, 7, 99)
	assert_int(plan[&"label"]).is_equal(7)
	assert_bool(plan[&"fresh"]).is_false()
	assert_int(plan[&"kind"]).is_equal(DriveKind.REPEAT)


# Before any input has ever been authored there is no label to repeat, so the
# pass falls back to the tick its timing carried. This is the only reason a
# kernel needs the timing at all.
func test_a_frame_before_any_input_labels_from_the_pass_timing() -> void:
	var plan := Engine_.predict_fold(-1, -1, 42)
	assert_int(plan[&"label"]).is_equal(42)
	assert_bool(plan[&"fresh"]).is_false()


# --- consume_plan ---


func test_authority_replays_only_past_the_standing_buffer() -> void:
	assert_int(Engine_.consume_plan(3, 1, true)[&"action"]).is_equal(
		ConsumeAction.REPLAY,
	)
	assert_int(Engine_.consume_plan(1, 1, true)[&"action"]).is_equal(
		ConsumeAction.HOLD,
	)


func test_authority_starves_only_with_nothing_queued() -> void:
	assert_int(Engine_.consume_plan(0, 0, true)[&"action"]).is_equal(
		ConsumeAction.STARVED,
	)
	assert_int(Engine_.consume_plan(1, 4, false)[&"action"]).is_equal(
		ConsumeAction.HOLD,
	)


# Pins a redundancy rather than a feature. The warm latch is set on exactly the
# test the replay branch then re-applies, so it cannot change any verdict today
# and a cold cursor decides identically to a warmed one. This law is here so that
# giving the latch a meaning has to be a deliberate act that breaks a stated
# expectation, rather than a quiet change to a condition nobody was watching.
func test_the_warm_latch_changes_no_verdict() -> void:
	for depth in [0, 1, 2, 5]:
		for buffer in [0, 1, 3]:
			var cold: Dictionary = Engine_.consume_plan(depth, buffer, false)
			var warm: Dictionary = Engine_.consume_plan(depth, buffer, true)
			assert_int(cold[&"action"]).override_failure_message(
				"depth %d buffer %d decided differently when warmed" % [depth, buffer],
			).is_equal(warm[&"action"])


func test_the_latch_warms_once_the_buffer_is_exceeded() -> void:
	assert_bool(Engine_.consume_plan(2, 1, false)[&"warmed"]).is_true()
	assert_bool(Engine_.consume_plan(1, 1, false)[&"warmed"]).is_false()
	assert_bool(Engine_.consume_plan(1, 1, true)[&"warmed"]).is_true()


# --- evaluate ---


# Every law in this block is about an out-of-domain transition, the tolerance
# compare. The exact branch and the choice between them are
# [TestPredictCompareSplit].
func _by_tolerance(
		predicted: Dictionary,
		payload: Dictionary,
		epsilon: float,
		sink: Dictionary,
) -> Dictionary:
	return Engine_.evaluate(
		Domain.OUT_OF_DOMAIN,
		ExactVerdict.UNJUDGED,
		predicted,
		payload,
		epsilon,
		{ },
		{ },
		{ },
		sink,
	)


func test_an_agreeing_transition_does_not_correct() -> void:
	var sink: Dictionary = { }
	var state := { &"x": 1.0 }
	var verdict := _by_tolerance(state, state, 0.1, sink)
	assert_bool(verdict[&"corrected"]).is_false()
	assert_float(verdict[&"divergence"]).is_equal_approx(0.0, 0.0001)


func test_a_transition_past_epsilon_corrects() -> void:
	var sink: Dictionary = { }
	var verdict := _by_tolerance({ &"x": 1.0 }, { &"x": 5.0 }, 0.1, sink)
	assert_bool(verdict[&"corrected"]).is_true()
	assert_float(verdict[&"divergence"]).is_equal_approx(4.0, 0.0001)
	assert_float(sink[&"x"]).is_equal_approx(4.0, 0.0001)


# An empty prediction means nothing was recorded at or before the ack, so there
# was no transition to judge. That must correct rather than pass as agreement,
# and it must leave the reported per-property errors alone, because reporting
# stale errors as this transition's would be worse than reporting none.
func test_an_unjudged_transition_corrects_and_reports_nothing_new() -> void:
	var sink: Dictionary = { &"stale": 9.0 }
	var verdict := _by_tolerance({ }, { &"x": 1.0 }, 0.1, sink)
	assert_bool(verdict[&"corrected"]).is_true()
	assert_float(verdict[&"divergence"]).is_equal(INF)
	assert_float(sink[&"stale"]).is_equal_approx(9.0, 0.0001)


# --- recover ---


# Every law below stages a recovery that is neither suppressed nor past the
# teleport tier unless it says so, so the tier arguments are bound here and the
# laws that are about them pass their own.
func _stage(
		payload: Dictionary,
		correction: PredictionHandle.CorrectionMode,
		snap_restore: PredictionHandle.RestoreMode,
		projection: Dictionary,
		ack_age_ticks: int,
		max_restore_ticks: int,
) -> Dictionary:
	return Engine_.recover(
		payload,
		# These laws are about the mechanism, so the policy is bound to whichever
		# repairing one names this mechanism. The policy that repairs nothing is
		# not reachable from a mechanism and has its own laws.
		(
				RecoveryPolicy.REBASE_REPLAY
				if correction == CorrectionMode.REPLAY
				else RecoveryPolicy.REBASE_RECOVER
		),
		correction,
		snap_restore,
		projection,
		{ },
		{ },
		{ },
		{ },
		0.0,
		1000.0,
		false,
		ack_age_ticks,
		max_restore_ticks,
		0.1,
	)


# A replay reaches the present under its own power, so the display is told
# nothing moved: the restore is a starting point, not a correction to absorb.
func test_replay_restores_but_reports_no_write() -> void:
	var payload := { &"x": 1.0 }
	var plan := _stage(
		payload, CorrectionMode.REPLAY, RestoreMode.EXACT, { }, 0, 8,
	)
	assert_dict(plan[&"restore"]).is_equal(payload)
	assert_dict(plan[&"write"]).is_empty()
	assert_bool(plan[&"skip"]).is_false()


func test_snap_reports_what_it_wrote() -> void:
	var payload := { &"x": 1.0 }
	var plan := _stage(
		payload, CorrectionMode.SNAP, RestoreMode.EXACT, { }, 0, 8,
	)
	assert_dict(plan[&"write"]).is_equal(payload)


# An EXTRAPOLATED snap lands the body near where it is now rather than at the
# stale acknowledgement, by carrying each property forward along the derivative
# channel it declared.
func test_an_extrapolated_snap_projects_along_declared_channels() -> void:
	var plan := _stage(
		{ &"pos": Vector3.ZERO, &"vel": Vector3(10.0, 0.0, 0.0) },
		CorrectionMode.SNAP,
		RestoreMode.EXTRAPOLATED,
		{ &"pos": &"vel" },
		2,
		8,
	)
	var restore: Dictionary = plan[&"restore"]
	assert_vector(restore[&"pos"]).is_equal_approx(Vector3(2.0, 0.0, 0.0), Vector3.ONE * 0.001)
	# The velocity itself is authoritative and is never projected.
	assert_vector(restore[&"vel"]).is_equal(Vector3(10.0, 0.0, 0.0))


# A backlogged acknowledgement would otherwise launch the body along a long
# straight line off a curved path, so the span it projects over is capped.
func test_the_projection_span_is_capped() -> void:
	var plan := _stage(
		{ &"pos": Vector3.ZERO, &"vel": Vector3(10.0, 0.0, 0.0) },
		CorrectionMode.SNAP,
		RestoreMode.EXTRAPOLATED,
		{ &"pos": &"vel" },
		1000,
		3,
	)
	var restore: Dictionary = plan[&"restore"]
	assert_vector(restore[&"pos"]).is_equal_approx(Vector3(3.0, 0.0, 0.0), Vector3.ONE * 0.001)


# --- the recovery tiers ---


func _tiered(
		withheld: Dictionary,
		pose_error: float,
		suppressed: bool,
) -> Dictionary:
	return Engine_.recover(
		{ &"pos": 1.0, &"vel": 2.0 },
		RecoveryPolicy.REBASE_RECOVER,
		CorrectionMode.SNAP,
		RestoreMode.EXACT,
		{ },
		withheld,
		{ },
		{ },
		{ },
		pose_error,
		10.0,
		suppressed,
		0,
		8,
		0.1,
	)


# A contractive field that re-converges on its own is worse off rewound to the
# acknowledged tick than left alone, so a sub-teleport recovery does not write
# it.
func test_a_sub_teleport_recovery_withholds_the_declared_fields() -> void:
	var plan := _tiered({ &"vel": true }, 1.0, false)
	assert_bool(plan[&"restore"].has(&"pos")).is_true()
	assert_bool(plan[&"restore"].has(&"vel")).is_false()
	assert_bool(plan[&"teleport"]).is_false()


# Past the threshold the predicted body holds nothing worth keeping, so the
# declaration stops applying and the whole closure is restored.
func test_a_teleport_restores_the_whole_closure() -> void:
	var plan := _tiered({ &"vel": true }, 50.0, false)
	assert_bool(plan[&"restore"].has(&"vel")).is_true()
	assert_bool(plan[&"teleport"]).is_true()


# A settling body and a diverging one look alike for a few ticks after a
# contact. The recovery declines rather than writing a value it would take back.
func test_a_suppressed_recovery_writes_nothing() -> void:
	var plan := _tiered({ }, 1.0, true)
	assert_bool(plan[&"skip"]).is_true()
	assert_dict(plan[&"restore"]).is_empty()
	assert_dict(plan[&"write"]).is_empty()


# Suppression is a pause on a transient, not on a desync, so it never outranks
# the teleport tier.
func test_suppression_never_outranks_the_teleport_tier() -> void:
	var plan := _tiered({ }, 50.0, true)
	assert_bool(plan[&"skip"]).is_false()
	assert_bool(plan[&"teleport"]).is_true()


# --- the observing policy ---


func _observed(pose_error: float, suppressed: bool) -> Dictionary:
	return Engine_.recover(
		{ &"pos": 1.0, &"vel": 2.0 },
		RecoveryPolicy.OBSERVE,
		CorrectionMode.SNAP,
		RestoreMode.EXACT,
		{ },
		{ },
		{ },
		{ },
		{ },
		pose_error,
		10.0,
		suppressed,
		0,
		8,
		0.1,
	)


# The policy reports a divergence and repairs nothing, so the recovery it stages
# is empty. Reporting has already happened by the time a recovery is staged,
# which is what leaves this policy with nothing to do.
func test_an_observing_policy_stages_nothing() -> void:
	var plan := _observed(1.0, false)
	assert_bool(plan[&"skip"]).is_true()
	assert_dict(plan[&"restore"]).is_empty()
	assert_dict(plan[&"write"]).is_empty()
	assert_bool(plan[&"teleport"]).is_false()


# The teleport tier outranks suppression because suppression pauses a transient.
# It does not outrank OBSERVE, because OBSERVE is not a pause: an entity that
# declared it never wanted the write, however far the pose has drifted.
func test_the_teleport_tier_never_outranks_an_observing_policy() -> void:
	var plan := _observed(50.0, false)
	assert_bool(plan[&"skip"]).override_failure_message(
		"a policy that repairs nothing repairs nothing at every pose error, or "
		+ "it is a tolerance wearing a policy's name",
	).is_true()
	assert_bool(plan[&"teleport"]).is_false()


func test_a_property_without_its_velocity_restores_verbatim() -> void:
	var payload := { &"pos": Vector3.ONE }
	var out := Engine_.project_payload(payload, { &"pos": &"vel" }, 0.5)
	assert_vector(out[&"pos"]).is_equal(Vector3.ONE)


func test_no_age_projects_nothing() -> void:
	var payload := { &"pos": Vector3.ZERO, &"vel": Vector3.ONE }
	var out := Engine_.project_payload(payload, { &"pos": &"vel" }, 0.0)
	assert_dict(out).is_equal(payload)

# --- converge_toward ---


# A field that drives the simulation but has no derivative to project along is
# already wrong by the time its acknowledged value lands, so writing it outright
# forks the body again. A bounded step closes the same error without ever
# writing a value the body was not near.
func test_a_converging_field_steps_part_of_the_way() -> void:
	var staged := Engine_.converge_toward(
		{ &"vel": 10.0 },
		{ &"vel": 0.0 },
		{ &"vel": 0.25 },
		{ },
	)
	assert_float(staged[&"vel"]).is_equal_approx(2.5, 0.0001)


# The rule reaches only the fields that declared it, so a rate never leaks onto
# a field the recipe did not name.
func test_converge_touches_only_the_fields_that_declared_it() -> void:
	var staged := Engine_.converge_toward(
		{ &"vel": 10.0, &"pos": 10.0 },
		{ &"vel": 0.0, &"pos": 0.0 },
		{ &"vel": 0.25 },
		{ },
	)
	assert_float(staged[&"pos"]).is_equal_approx(10.0, 0.0001)


# The degenerate rates are the declaration opting out, and both must restore
# outright rather than stepping by nothing or by everything-plus-rounding.
func test_the_degenerate_rates_restore_outright() -> void:
	for rate in [0.0, 1.0]:
		var staged := Engine_.converge_toward(
			{ &"vel": 10.0 }, { &"vel": 0.0 }, { &"vel": rate }, { },
		)
		assert_float(staged[&"vel"]).override_failure_message(
			"rate %.1f must restore outright" % rate,
		).is_equal_approx(10.0, 0.0001)


# A type with no meaningful midpoint cannot be stepped, and dropping it would
# leave the body holding a predicted value it was never entitled to keep, so it
# restores outright instead.
func test_an_unsteppable_type_restores_outright() -> void:
	var staged := Engine_.converge_toward(
		{ &"flag": true }, { &"flag": false }, { &"flag": 0.5 }, { },
	)
	assert_bool(staged[&"flag"]).is_true()


# A heading crossing +/- PI must step along its true arc. Without the angle
# declaration the step would take the long way round, moving the body most of a
# turn to close an error of a few degrees.
func test_a_converging_angle_steps_the_short_way() -> void:
	var staged := Engine_.converge_toward(
		{ &"heading": -3.0 },
		{ &"heading": 3.0 },
		{ &"heading": 0.5 },
		{ &"heading": true },
	)
	# The short arc from 3.0 to -3.0 is about +0.283, so half of it leaves the
	# heading just past PI rather than back down near zero.
	assert_float(staged[&"heading"]).is_equal_approx(3.1416, 0.01)


# The step is complete when the recovery is. Nothing is left outstanding, so
# staging the same recovery twice from its own result converges rather than
# reopening a window that keeps writing between recoveries.
func test_the_step_leaves_nothing_outstanding() -> void:
	var current := { &"vel": 0.0 }
	for _i in range(30):
		current = Engine_.converge_toward(
			{ &"vel": 10.0 }, current, { &"vel": 0.25 }, { },
		)
	assert_float(current[&"vel"]).is_equal_approx(10.0, 0.01)
