## Unit tests for [InterestService] composition helpers.
class_name TestInterestService
extends NetwTestSuite

var mt: MultiplayerTree
var service: InterestService


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.get_service(InterestService) as InterestService


func test_ancestors_admit_requires_every_gate_verdict() -> void:
	var scene := make_test_entity(mt, "Scene", 0, false)
	var gate := _make_gate(&"scene")
	scene.add_child(gate)
	NetwEntity.of(scene).provide(NetwEntity.Slot.INTEREST_GATE, gate)

	var child := make_test_entity(scene, "Child", 0, false)
	var child_entity := NetwEntity.of(child)

	assert_that(service._ancestors_admit(7, child_entity)).is_false()

	gate.viewers = PackedInt32Array([7])

	assert_that(service._ancestors_admit(7, child_entity)).is_true()


func test_gate_dirty_marks_descendant_filtered_entities_dirty() -> void:
	var scene := make_test_entity(mt, "Scene", 0, false)
	var gate := _make_gate(&"scene")
	scene.add_child(gate)
	NetwEntity.of(scene).provide(NetwEntity.Slot.INTEREST_GATE, gate)
	service._gates[&"scene"] = gate

	var child := make_test_entity(scene, "Child")
	var child_entity := NetwEntity.of(child)
	var layer := service.layer_for(&"own")
	layer.add_entity(child_entity)
	service._dirty_entities.clear()

	service._mark_gate_dirty(&"scene")

	assert_that(service._dirty_entities.has(child_entity)).is_true()


func _make_gate(layer_id: StringName) -> InterestGate:
	var gate := InterestGate.new()
	gate.name = "Gate"
	gate.layer_id = layer_id
	return auto_free(gate)
