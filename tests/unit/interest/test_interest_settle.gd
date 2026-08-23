## The settle conversion's worked example: interest visibility, observed with no
## frame.
##
## [method NetwMultiplayer.interest_flush] has always been the escape hatch, and a test
## calling it proves nothing about the cadence a game gets. What these cases
## pin is the cadence itself: a mutation schedules a flush, and the flush lands
## at the session's own settle, with nothing awaited and no frame driven. That
## is the property the deferred call could not have, because a tier that cannot
## flush the message queue cannot see a deferred effect land at all.
class_name TestInterestSettle
extends NetwTestSuite

var mt: MultiplayerTree
var service: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.api


## A mutation queues the flush under the interface's own key and does not run it
## on the spot, so a cascade of mutations still costs one flush.
func test_a_mutation_schedules_the_flush_rather_than_running_it() -> void:
	var entity := NetwEntity.of(make_test_entity(mt, "Subject", 0, false))
	var layer: NetwInterestLayer = service._native_core.interest_layer(&"sight")
	layer.add_entity(entity)
	layer.add_viewer(7)
	layer.add_viewer(8)

	assert_int(_queued_flushes()).is_equal(1)
	assert_bool(service._native_core.interest_participant_sees(7, entity)).is_false()


## The settle is what lands it, and the settle is reachable without a frame.
func test_the_flush_lands_at_the_settle_with_no_frame() -> void:
	var entity := NetwEntity.of(make_test_entity(mt, "Subject", 0, false))
	var layer: NetwInterestLayer = service._native_core.interest_layer(&"sight")
	layer.add_entity(entity)
	layer.add_viewer(7)

	mt.api._settle()

	assert_int(_queued_flushes()).is_equal(0)
	assert_bool(service._native_core.interest_participant_sees(7, entity)).is_true()


## Flushing on the spot withdraws the queued flush, so the next settle is not a
## second recompute of state that already committed.
func test_flush_now_leaves_nothing_for_the_settle() -> void:
	var entity := NetwEntity.of(make_test_entity(mt, "Subject", 0, false))
	var layer: NetwInterestLayer = service._native_core.interest_layer(&"sight")
	layer.add_entity(entity)
	layer.add_viewer(7)

	service.interest_flush()

	assert_int(_queued_flushes()).is_equal(0)
	assert_bool(service._native_core.interest_participant_sees(7, entity)).is_true()


# How many interest flushes the session has queued for its next settle.
func _queued_flushes() -> int:
	var pending := mt.api._native_core.settle_has_key(
		NetwMultiplayerCore.interest_flush_key(),
	)
	return 1 if pending else 0
