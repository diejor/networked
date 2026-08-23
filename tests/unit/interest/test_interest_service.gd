## Unit tests for the session interest plane composition helpers.
class_name TestInterestService
extends NetwTestSuite

var mt: MultiplayerTree
var service: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.api


func test_synchronizer_visibility_event_updates_committed_intent() -> void:
	var root := make_test_entity(mt, "IntentRoot", 0, false)
	var entity := NetwEntity.of(root)
	var layer: NetwInterestLayer = service._native_core.interest_layer(&"sight")
	layer.add_entity(entity)
	layer.add_viewer(7)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = ^".."
	sync.public_visibility = false
	root.add_child(sync)
	mt.api.object_configuration_add(root, sync)
	service.interest_flush()
	assert_that(service._native_core.interest_participant_sees(7, entity)).is_false()

	sync.set_visibility_for(7, true)
	service.interest_flush()
	assert_that(service._native_core.interest_participant_sees(7, entity)).is_true()


func test_shared_entities_follow_the_resolved_interest_scope() -> void:
	var subject := NetwEntity.of(make_test_entity(mt, "Subject", 0, false))
	var shared := NetwEntity.of(make_test_entity(mt, "Shared", 0, false))
	var other := NetwEntity.of(make_test_entity(mt, "Other", 0, false))
	subject.entity_id = &"subject"
	shared.entity_id = &"b_shared"
	other.entity_id = &"a_other"
	service._native_core.interest_layer(&"race").add_entity(subject)
	service._native_core.interest_layer(&"race").add_entity(shared)
	service._native_core.interest_layer(&"lobby").add_entity(subject)
	service._native_core.interest_layer(&"lobby").add_entity(other)

	assert_array(service._native_core.interest_resolved_layer_ids(subject)) \
			.contains_exactly([&"lobby", &"race"])
	assert_array(service._native_core.interest_shared_entities(subject)) \
			.contains_exactly([other, shared])
	assert_array(service._native_core.interest_shared_entities(subject, &"lobby")) \
			.contains_exactly([other])
