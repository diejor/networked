## Tier B spike: timeline semantics (architecture P1 substrate).
##
## Pure unit tests on [SpikeTimeline] and [SpikeSim]. No network. Falsifies the
## carry-forward asymmetry, the record-at-t+1 discipline, and the ack-trim
## watermark before any networked tier depends on them.
class_name TestSpikeTimeline
extends NetwTestSuite


func test_b1_state_carries_forward_input_does_not() -> void:
	var tl := SpikeTimeline.new()
	tl.record_state(2, {&"position": Vector2(2, 2)})
	tl.record_state(4, {&"position": Vector2(4, 4)})
	tl.record_state(8, {&"position": Vector2(8, 8)})
	tl.record_input(2, {&"mx": 1.0})
	tl.record_input(4, {&"mx": -1.0})

	# State carries forward: tick 5 has no record, reads tick 4's value.
	assert_that(tl.latest_state_at_or_before(5)).is_equal(
		{&"position": Vector2(4, 4)},
	)
	assert_that(tl.has_state_at(5)).is_false()

	# Input is exact-only: tick 5 has no input, reads neutral, never tick 4's.
	assert_that(tl.input_at(5)).is_equal(SpikeTimeline.NEUTRAL)
	assert_that(tl.has_input_at(5)).is_false()
	assert_that(tl.input_at(4)).is_equal({&"mx": -1.0})


func test_b1_below_oldest_state_is_neutral() -> void:
	var tl := SpikeTimeline.new()
	tl.record_state(10, {&"position": Vector2(10, 0)})
	assert_that(tl.latest_state_at_or_before(5)).is_equal(SpikeTimeline.NEUTRAL)


func test_b2_record_at_t_plus_one_matches_replay() -> void:
	# state(t+1) = simulate(state(t), input(t)). The same input stream, recorded
	# authoritatively and re-derived by replay, must produce identical state.
	var inputs := {
		0: {&"mx": 1.0, &"my": 0.0},
		1: {&"mx": 1.0, &"my": 0.0},
		2: {&"mx": 0.0, &"my": 1.0},
		3: {&"mx": -1.0, &"my": 0.0},
	}
	var delta := 1.0 / 30.0

	var authoritative := SpikeTimeline.new()
	var pos := Vector2.ZERO
	authoritative.record_state(0, {&"position": pos})
	for t in range(0, 4):
		pos = SpikeSim.integrate(pos, inputs[t], delta)
		authoritative.record_input(t, inputs[t])
		authoritative.record_state(t + 1, {&"position": pos})

	# Replay from the recorded state(0) over the recorded input window.
	var replay_pos: Vector2 = authoritative.state_at(0)[&"position"]
	for entry in authoritative.inputs_in_range(0, 3):
		replay_pos = SpikeSim.integrate(replay_pos, entry["input"], delta)
		var recorded: Vector2 = authoritative.state_at(entry["tick"] + 1)[&"position"]
		# Identical op sequence, so equality is exact, not approximate.
		assert_vector(replay_pos).is_equal(recorded)


func test_b3_trim_preserves_replay_window() -> void:
	var tl := SpikeTimeline.new()
	for t in range(0, 40):
		tl.record_state(t, {&"position": Vector2(t, 0)})
		tl.record_input(t, {&"mx": 1.0})

	var ack := 20
	tl.trim_before(ack)

	# The replay window above the ack stays complete and contiguous.
	var window := tl.inputs_in_range(ack + 1, 39)
	assert_that(window.size()).is_equal(39 - ack)
	var expected_tick := ack + 1
	for entry in window:
		assert_that(entry["tick"]).is_equal(expected_tick)
		expected_tick += 1

	# Below the floor is gone; the oldest readable state is above the ack.
	assert_that(tl.has_state_at(ack)).is_false()
	assert_that(tl.latest_state_at_or_before(ack)).is_equal(SpikeTimeline.NEUTRAL)
	assert_that(tl.oldest_state_tick()).is_greater(ack)


func test_b3_inputs_in_range_skips_holes() -> void:
	# Dropped input ticks leave holes; the window reflects them honestly rather
	# than carrying a neighbor forward.
	var tl := SpikeTimeline.new()
	tl.record_input(1, {&"mx": 1.0})
	tl.record_input(3, {&"mx": 1.0})
	var window := tl.inputs_in_range(1, 3)
	assert_that(window.size()).is_equal(2)
	assert_that(window[0]["tick"]).is_equal(1)
	assert_that(window[1]["tick"]).is_equal(3)
