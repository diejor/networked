## Unit tests for the [NetwEntity] lag-compensation slots.
##
## Covers the timeline slot over the generic provide/slot plumbing, and the lazy
## prediction handle that reports unregistered until an engine wires.
class_name TestNetwEntitySlots
extends NetwTestSuite

func test_slots_provide_and_retrieve() -> void:
	var entity := NetwEntity.new()
	var timeline := NetwTimeline.new()

	entity.timeline = timeline

	assert_bool(entity.timeline == timeline).is_true()
	# The prediction handle is lazily created and never null, but reports no engine
	# until register_prediction wires one.
	assert_bool(entity.prediction != null).is_true()
	assert_bool(entity.prediction.is_registered()).is_false()
