## Unit tests for [NetwTimeline].
##
## Covers the carry-forward asymmetry (state carries forward, input is exact),
## the record-at-t+1 discipline, the reconciliation replay window, and the trim
## watermark. Pure RefCounted, no harness.
class_name TestNetwTimeline
extends NetwTestSuite

func test_state_at_is_exact() -> void:
	var t := NetwTimeline.new()
	t.record_state(5, { &"position": Vector2(5, -5) })

	assert_vector(t.state_at(5).get(&"position")).is_equal(Vector2(5, -5))
	assert_dict(t.state_at(4)).is_empty()
	assert_dict(t.state_at(6)).is_empty()


func test_state_carries_forward() -> void:
	var t := NetwTimeline.new()
	t.record_state(5, { &"position": Vector2(5, -5) })
	t.record_state(8, { &"position": Vector2(8, -8) })

	# A gap reads as the newest snapshot at or before the query tick.
	assert_vector(
		t.latest_state_at_or_before(7).get(&"position"),
	).is_equal(Vector2(5, -5))
	assert_vector(
		t.latest_state_at_or_before(8).get(&"position"),
	).is_equal(Vector2(8, -8))
	# Nothing at or before the query tick is an empty snapshot.
	assert_dict(t.latest_state_at_or_before(4)).is_empty()


func test_input_is_exact_and_never_carries_forward() -> void:
	var t := NetwTimeline.new()
	t.record_input(5, { &"motion": Vector2.RIGHT })
	t.record_input(7, { &"motion": Vector2.LEFT })

	assert_vector(t.input_at(5).get(&"motion")).is_equal(Vector2.RIGHT)
	# A missing input tick is a deliberate no-action, not a stale repeat.
	assert_dict(t.input_at(6)).is_empty()
	assert_vector(t.input_at(7).get(&"motion")).is_equal(Vector2.LEFT)


func test_record_at_t_plus_one_is_retrievable() -> void:
	# state(t+1) = simulate(state(t), input(t)): the snapshot lands at t+1.
	var t := NetwTimeline.new()
	t.record_input(3, { &"motion": Vector2.DOWN })
	t.record_state(4, { &"position": Vector2(0, 10) })

	assert_vector(t.input_at(3).get(&"motion")).is_equal(Vector2.DOWN)
	assert_vector(t.state_at(4).get(&"position")).is_equal(Vector2(0, 10))


func test_inputs_in_range_is_ordered_and_skips_gaps() -> void:
	var t := NetwTimeline.new()
	t.record_input(2, { &"motion": Vector2(2, 0) })
	t.record_input(4, { &"motion": Vector2(4, 0) })
	t.record_input(5, { &"motion": Vector2(5, 0) })

	var window := t.inputs_in_range(2, 5)
	assert_int(window.size()).is_equal(3)
	assert_int(window[0].tick).is_equal(2)
	assert_int(window[1].tick).is_equal(4)
	assert_int(window[2].tick).is_equal(5)
	assert_vector(window[2].input.get(&"motion")).is_equal(Vector2(5, 0))


func test_has_input_at_and_newest_input_tick_drive_consume() -> void:
	# The server consume step distinguishes a lost tick (a later input exists)
	# from one that has not arrived yet via these two accessors.
	var t := NetwTimeline.new()
	assert_int(t.newest_input_tick()).is_equal(-1)
	t.record_input(2, { &"motion": Vector2.RIGHT })
	t.record_input(4, { &"motion": Vector2.RIGHT })

	assert_bool(t.has_input_at(2)).is_true()
	# Tick 3 is missing but a later input (4) exists: a genuine hole.
	assert_bool(t.has_input_at(3)).is_false()
	assert_int(t.newest_input_tick()).is_equal(4)


func test_trim_before_masks_older_entries() -> void:
	var t := NetwTimeline.new()
	t.record_state(3, { &"position": Vector2(3, 0) })
	t.record_state(6, { &"position": Vector2(6, 0) })
	t.record_input(3, { &"motion": Vector2.UP })
	t.record_input(6, { &"motion": Vector2.DOWN })

	t.trim_before(6)

	# Entries before the watermark read as absent on every accessor.
	assert_dict(t.state_at(3)).is_empty()
	assert_dict(t.latest_state_at_or_before(5)).is_empty()
	assert_dict(t.input_at(3)).is_empty()
	# The watermark tick and newer survive.
	assert_vector(t.state_at(6).get(&"position")).is_equal(Vector2(6, 0))
	assert_int(t.inputs_in_range(3, 6).size()).is_equal(1)
	assert_int(t.inputs_in_range(3, 6)[0].tick).is_equal(6)


func test_trim_watermark_only_advances() -> void:
	var t := NetwTimeline.new()
	t.record_state(4, { &"position": Vector2(4, 0) })
	t.trim_before(6)
	t.trim_before(2) # must not rewind the floor

	assert_dict(t.state_at(4)).is_empty()
