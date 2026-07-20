## History-layer semantics the display calculus owns as pure kernel truths.
##
## These are the tick-domain and sleep/revive behaviors that the node-wired
## integration suites exercise end to end. The wiring (consumed feeds, interest
## flap, authoring stamps) stays their concern; the display meaning is pinned here
## against the [code]_History[/code] kernel with no scene, so a break names the
## kernel rule rather than a flaky feed.
class_name TestNetwInterpHistorySemantics
extends NetwTestSuite


func _history() -> NetwInterpolationInterface._History:
	var h := NetwInterpolationInterface._History.new()
	h.mode = NetwInterpolate.Mode.LERP
	return h


# The tick-domain property: a history fed authoring ticks keys and brackets by
# those ticks, so the displayed value names an authoring tick under the playhead.
# This is the semantic half of the authoring-tick integration suite.
func test_history_keys_and_interpolates_by_authoring_tick() -> void:
	var h := _history()
	h.record(100, Vector2(0.0, 0.0), true)
	h.record(103, Vector2(30.0, 0.0), true)

	var bracket := h.bracketing_ticks(101)
	assert_int(bracket.x).is_equal(100)
	assert_int(bracket.y).is_equal(103)

	# dt 101 sits a third of the way across the 100..103 authoring gap.
	var shown: Variant = h.sample(101, 0.0, Vector2(0.0, 0.0), 3)
	assert_vector(shown).is_equal_approx(Vector2(10.0, 0.0), Vector2(0.5, 0.5))


# A history whose stream holds a settled value falls asleep so the pump defers to
# other writers, then wakes on the next distinct record and resumes display. This
# is the semantic half of the consumed-interpolation revival suite.
func test_history_sleeps_when_settled_then_revives_on_new_record() -> void:
	var h := _history()
	var settled := Vector2(5.0, 0.0)
	h.record(0, settled, false)

	# Sampling past the newest tick while the display already shows it sleeps it.
	var held: Variant = h.sample(4, 0.0, settled, 3)
	assert_vector(held).is_equal(settled)
	assert_bool(h.is_sleeping).is_true()

	# A new distinct record is the revival: the history wakes immediately.
	var revived := Vector2(50.0, 0.0)
	h.record(10, revived, false)
	assert_bool(h.is_sleeping).is_false()

	var shown: Variant = h.sample(10, 0.0, settled, 3)
	assert_vector(shown).is_equal(revived)


# A duplicate value must not revive a sleeping history, or a quiet stream that
# keeps resending the same value would never let another writer take over.
func test_duplicate_record_does_not_wake_a_sleeping_history() -> void:
	var h := _history()
	var settled := Vector2(5.0, 0.0)
	h.record(0, settled, false)
	h.sample(4, 0.0, settled, 3)
	assert_bool(h.is_sleeping).is_true()

	h.record(1, settled, false)
	assert_bool(h.is_sleeping).is_true()
