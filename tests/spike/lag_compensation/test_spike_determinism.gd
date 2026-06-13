## Tier 1.1 spike: the determinism gate (the gate's own gate).
##
## Under [LockstepStepper] there is exactly one synchronizer send per tick and
## [method MultiplayerClock.force_step] advances an exact tick count with no
## frame-cadence overshoot. The per-tick sync multiplicity that used to vary with
## headless frame cadence is gone, so the seeded RNG draws against a fixed packet
## count and the exact-regime outcome is reproducible to floating point. The
## seeded impairment is genuinely seed-driven, not random.
class_name TestSpikeDeterminism
extends NetwTestSuite


func _const_right(_tick: int) -> Dictionary:
	return {&"mx": 1.0, &"my": 0.0}


func _run_exact_outcome() -> Vector2:
	var rig := SpikePredictRig.new()
	await rig.setup(self, _const_right, 30, 3, false)
	rig.delay_both(4)
	rig.sync_ticks(80)
	var pos := rig.client_body.position
	# corrections must be zero in the exact regime, by construction.
	assert_that(rig.predictor.corrections).is_equal(0)
	await rig.inner.teardown()
	return pos


func _delivered_count(_seed: int) -> int:
	var h := SpikeNetHarness.new()
	await h.setup(self, 30, 3, true, false)
	h.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			h.server_sync.authored_tick = t
			h.server_node.position = Vector2(t, -t),
	)
	h.delay_server_to_client(2, _seed, 0, 0.2)
	h.sync_ticks(120)
	var count := 0
	for c: Dictionary in h.client_sync.captures:
		if int(c["tick"]) >= 0:
			count += 1
	await h.inner.teardown()
	return count


const STEP := SpikeSim.SPEED / 30.0


func test_1_1_exact_regime_outcome_is_reproducible() -> void:
	# Same input, same exact delay, two independent runs. Each run force-steps an
	# identical tick count along the closed-form ray, so the outcomes agree to
	# floating point with no one-tick stepper slack.
	var a := await _run_exact_outcome()
	var b := await _run_exact_outcome()
	assert_that(a.x).is_greater(0.0)
	_assert_on_grid(a)
	_assert_on_grid(b)
	assert_that(absf(a.x - b.x)).is_less_equal(0.01)
	assert_that(absf(a.y - b.y)).is_less_equal(0.01)


func _assert_on_grid(p: Vector2) -> void:
	assert_that(absf(p.y)).is_less(0.01)
	var ticks: float = p.x / STEP
	assert_that(absf(ticks - roundf(ticks))).is_less(0.01)


func test_1_1_seed_drives_loss_pattern() -> void:
	# A different seed drops a different set of packets, so the delivered count
	# changes. This proves the impairment is seeded, not fixed or absent.
	var loss_a := await _delivered_count(7)
	var loss_b := await _delivered_count(99)
	assert_that(loss_a).is_greater(0)
	assert_that(loss_b).is_greater(0)
	assert_that(loss_a).is_not_equal(loss_b)
