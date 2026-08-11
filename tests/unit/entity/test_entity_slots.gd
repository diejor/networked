## Laws for the session's one entity numbering.
##
## The book exists because two cores need the same key on different cadences.
## [InterestCore] retires an entity when its owner leaves the tree, and a
## prediction engine is released only by an explicit
## [method NetwMultiplayer.predict_undeclare], so the first holder to let go
## must not be able to retire a number the second still reads.
class_name TestEntitySlots
extends NetwTestSuite

const INTEREST := &"interest"
const PREDICT := &"predict"


func _entity() -> NetwEntity:
	var node: Node2D = auto_free(Node2D.new())
	add_child(node)
	return NetwEntity.ensure(node)


func test_a_slot_is_minted_once_and_read_without_minting() -> void:
	var slots := NetwEntitySlots.new()
	var entity := _entity()
	assert_int(slots.slot_of(entity)).is_equal(0)
	var slot := slots.ensure(entity, INTEREST)
	assert_int(slot).is_greater(0)
	assert_int(slots.ensure(entity, INTEREST)).is_equal(slot)
	assert_int(slots.slot_of(entity)).is_equal(slot)
	assert_object(slots.entity_for(slot)).is_same(entity)


func test_one_holder_releasing_retires_nothing() -> void:
	var slots := NetwEntitySlots.new()
	var entity := _entity()
	var slot := slots.ensure(entity, INTEREST)
	slots.ensure(entity, PREDICT)

	slots.release(entity, INTEREST)
	slots.sweep()

	assert_bool(slots.holds(entity, INTEREST)).is_false()
	assert_bool(slots.holds(entity, PREDICT)).is_true()
	assert_int(slots.slot_of(entity)).override_failure_message(
		"interest's sweep took a number prediction still reads",
	).is_equal(slot)
	assert_object(slots.entity_for(slot)).is_same(entity)


func test_the_last_holder_releasing_retires_the_slot() -> void:
	var slots := NetwEntitySlots.new()
	var entity := _entity()
	var slot := slots.ensure(entity, INTEREST)
	slots.ensure(entity, PREDICT)

	slots.release(entity, INTEREST)
	slots.release(entity, PREDICT)
	assert_int(slots.slot_of(entity)).override_failure_message(
		"a retired slot must survive its own release by one full cycle, "
		+ "because the delta naming it has not been applied yet",
	).is_equal(slot)

	slots.sweep()
	assert_int(slots.slot_of(entity)).is_equal(0)
	assert_object(slots.entity_for(slot)).is_null()


func test_a_released_slot_is_never_handed_out_again() -> void:
	var slots := NetwEntitySlots.new()
	var first := _entity()
	var retired := slots.ensure(first, INTEREST)
	slots.release(first, INTEREST)
	slots.sweep()

	assert_int(slots.ensure(_entity(), INTEREST)).override_failure_message(
		"a reused slot lets a late delta resolve to an entity that never "
		+ "earned it",
	).is_not_equal(retired)


func test_re_holding_a_retired_slot_cancels_its_sweep() -> void:
	var slots := NetwEntitySlots.new()
	var entity := _entity()
	var slot := slots.ensure(entity, INTEREST)
	slots.release(entity, INTEREST)

	assert_int(slots.ensure(entity, PREDICT)).is_equal(slot)
	slots.sweep()
	assert_int(slots.slot_of(entity)).is_equal(slot)


func test_a_new_session_is_a_new_numbering() -> void:
	var slots := NetwEntitySlots.new()
	slots.ensure(_entity(), INTEREST)
	var entity := _entity()
	var before := slots.ensure(entity, INTEREST)
	slots.clear()

	assert_int(slots.slot_of(entity)).is_equal(0)
	assert_int(slots.ensure(_entity(), INTEREST)).is_less_equal(before)
