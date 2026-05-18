## Unit tests for [NetwInterest].
class_name TestNetwInterest
extends NetworkedTestSuite


var mt: MultiplayerTree
var interest: NetwInterest


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	interest = mt.interest


func _make_entity(entity_name: String = "ent") -> NetwEntity:
	return NetwEntity.of(make_test_entity(mt, entity_name, 0, false))


func test_layer_for_returns_stable_instance() -> void:
	var a := interest.layer_for(&"a")
	var b := interest.layer_for(&"a")
	assert_that(a).is_equal(b)


func test_get_layer_returns_null_for_missing_layer() -> void:
	assert_that(interest.get_layer(&"missing")).is_null()


func test_register_entity_creates_layer_membership() -> void:
	var entity := _make_entity()
	interest.register_entity_for_layer(&"a", entity)
	assert_that(interest.layer_for(&"a").has_entity(entity)).is_true()


func test_unregister_entity_removes_layer_membership() -> void:
	var entity := _make_entity()
	interest.register_entity_for_layer(&"a", entity)
	interest.unregister_entity_from_layer(&"a", entity)
	assert_that(interest.layer_for(&"a").has_entity(entity)).is_false()


func test_can_peer_see_entity_uses_union_semantics() -> void:
	var entity := _make_entity()
	interest.register_entity_for_layer(&"hidden", entity)
	interest.register_entity_for_layer(&"visible", entity)
	interest.add_viewer(&"visible", 7)

	assert_that(interest.can_peer_see_entity(7, entity)).is_true()


func test_can_peer_see_entity_rejects_without_visible_layer() -> void:
	var entity := _make_entity()
	interest.register_entity_for_layer(&"hidden", entity)

	assert_that(interest.can_peer_see_entity(7, entity)).is_false()


func test_receive_viewer_delta_updates_mirrored_layer() -> void:
	interest.receive_viewer_delta(&"mirror", 7, true)
	assert_that(interest.layer_for(&"mirror").has_viewer(7)).is_true()


func test_receive_layer_config_updates_mirrored_policy() -> void:
	interest.receive_layer_config(
			&"mirror", InterestSynchronizer.Policy.HIDE_FROM_INSIDERS)
	assert_that(interest.layer_for(&"mirror").policy).is_equal(
			InterestSynchronizer.Policy.HIDE_FROM_INSIDERS)
