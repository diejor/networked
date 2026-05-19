## Unit tests for [InterestGate]. Covers bind/unbind, server-side
## write-through from the bound layer to the gate's spawn-synced
## properties, and the spawner-filter Callable.
class_name TestInterestGate
extends NetworkedTestSuite


var mt: MultiplayerTree


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)


func _make_gate(id: StringName, parent: Node = null) -> InterestGate:
	var gate := InterestGate.new()
	gate.name = "Gate_" + str(id)
	gate.layer_id = id
	var p: Node = parent if parent else mt
	p.add_child(gate)
	auto_free(gate)
	return gate


func test_gate_binds_layer_on_enter_tree() -> void:
	var gate := _make_gate(&"a")
	var layer := mt.interest.get_layer(&"a")
	assert_that(layer).is_not_null()
	assert_that(layer.bound_gate()).is_equal(gate)


func test_gate_unbinds_on_exit_tree() -> void:
	var gate := _make_gate(&"a")
	var layer := mt.interest.get_layer(&"a")
	mt.remove_child(gate)
	assert_that(layer.bound_gate()).is_null()


func test_layer_add_viewer_writes_through_to_gate() -> void:
	var gate := _make_gate(&"a")
	var layer := mt.interest.layer(&"a")
	layer.add_viewer(7)
	layer.add_viewer(11)
	assert_that(gate.has_viewer(7)).is_true()
	assert_that(gate.has_viewer(11)).is_true()


func test_layer_remove_viewer_writes_through_to_gate() -> void:
	var gate := _make_gate(&"a")
	var layer := mt.interest.layer(&"a")
	layer.add_viewer(7)
	layer.remove_viewer(7)
	assert_that(gate.has_viewer(7)).is_false()


func test_layer_set_policy_writes_through_to_gate() -> void:
	var gate := _make_gate(&"a")
	var layer := mt.interest.layer(&"a")
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	assert_that(gate.policy).is_equal(
			NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)


func test_gate_picks_up_initial_layer_state_on_bind() -> void:
	var layer := mt.interest.layer(&"a")
	layer.add_viewer(7)
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	var gate := _make_gate(&"a")
	assert_that(gate.has_viewer(7)).is_true()
	assert_that(gate.policy).is_equal(
			NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)


func test_spawner_filter_reflects_policy() -> void:
	var gate := _make_gate(&"a")
	var layer := mt.interest.layer(&"a")
	var filter := gate.make_spawner_filter()
	assert_that(filter.call(7)).is_false()
	layer.add_viewer(7)
	assert_that(filter.call(7)).is_true()


func test_second_gate_for_same_layer_errors() -> void:
	_make_gate(&"a")
	var second := InterestGate.new()
	second.name = "SecondA"
	second.layer_id = &"a"
	# Adding to tree triggers register_gate which should push_error.
	# We tolerate the error; the gate just shouldn't replace the first.
	mt.add_child(second)
	auto_free(second)
	var registered: InterestGate = mt.get_service(InterestService) \
			.gate_for(&"a")
	# First gate remains the registered one.
	assert_that(registered).is_not_equal(second)
