## Real-node determinism gate (ports the lag-comp spike determinism tier).
##
## Under [LockstepStepper] there is exactly one synchronizer send per tick and
## [method MultiplayerClock.force_step] advances an exact tick count with no
## frame-cadence overshoot, so the seeded impairment draws against a fixed packet
## count and the outcome is reproducible to floating point. The same scenario run
## twice produces the same final state and the same correction trace.
class_name TestPredictionDeterminism
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const STEP := SpikeSim.SPEED / 30.0


func test_exact_regime_outcome_is_reproducible() -> void:
	# Same input, same exact delay, two independent runs. Each force-steps an
	# identical tick count along the closed-form ray, so the outcomes agree to
	# floating point and no correction ever fires.
	var a := await _run_exact_outcome()
	var b := await _run_exact_outcome()
	assert_that(a.x).is_greater(0.0)
	_assert_on_grid(a)
	_assert_on_grid(b)
	assert_that(absf(a.x - b.x)).is_less_equal(0.01)
	assert_that(absf(a.y - b.y)).is_less_equal(0.01)


func test_seeded_trace_is_reproducible() -> void:
	# Identical seeded jitter and loss produces an identical correction trace, so
	# the impairment is genuinely seed-driven, not run-to-run random.
	var a := await _run_impaired_trace()
	var b := await _run_impaired_trace()
	assert_int(a["corrections"]).is_equal(b["corrections"])
	assert_int((a["corrected"] as Array).size()) \
			.is_equal((b["corrected"] as Array).size())
	assert_that(a["corrected"]).is_equal(b["corrected"])


func _run_exact_outcome() -> Vector2:
	var s := PredictionScenario.new()
	await s.setup(self, 30, 3, false)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.warmup(p)
	s.run(80)
	var pos: Vector2 = p.client_body.position
	# Steady-state exact delivery never corrects.
	assert_int(p.corrections).is_equal(0)
	await s.teardown()
	return pos


func _run_impaired_trace() -> Dictionary:
	var s := PredictionScenario.new()
	await s.setup(self, 30, 3, false)
	var p := await s.add_predicted_entity()
	s.latency_both(3, 6, 0.1)
	s.hold_input(p, RIGHT)
	s.run(120)
	var corrected: Array[bool] = []
	for entry in p.observer.divergence_log:
		corrected.append(entry[&"corrected"])
	var trace := {
		"corrections": p.corrections,
		"corrected": corrected,
	}
	await s.teardown()
	return trace


func _assert_on_grid(pos: Vector2) -> void:
	assert_that(absf(pos.y)).is_less(0.01)
	var ticks: float = pos.x / STEP
	assert_that(absf(ticks - roundf(ticks))).is_less(0.01)
