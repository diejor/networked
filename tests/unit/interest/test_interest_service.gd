## Unit tests for [NetwInterestInterface] composition helpers.
class_name TestInterestService
extends NetwTestSuite

var mt: MultiplayerTree
var service: NetwInterestInterface


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.api.interest


func test_synchronizer_visibility_event_updates_committed_intent() -> void:
	var root := make_test_entity(mt, "IntentRoot", 0, false)
	var entity := NetwEntity.of(root)
	var layer := service.layer(&"sight")
	layer.add_entity(entity)
	layer.add_viewer(7)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = ^".."
	sync.public_visibility = false
	root.add_child(sync)
	mt.api.replication._sync_compat.consume(root, sync)
	service.flush_now()
	assert_that(service.participant_sees(7, entity)).is_false()

	sync.set_visibility_for(7, true)
	service.flush_now()
	assert_that(service.participant_sees(7, entity)).is_true()
