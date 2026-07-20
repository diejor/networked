## Unit tests for [NetwInterestLayer]. Covers the canonical mutation
## API exercised standalone (no [NetwInterestInterface]) so the data model
## is testable in isolation.
class_name TestNetwInterestLayer
extends NetwTestSuite

var layer: NetwInterestLayer


func before_test() -> void:
	layer = NetwInterestLayer.new(&"test")


func _make_entity(entity_name: String = "ent") -> NetwEntity:
	var root := Node.new()
	root.name = entity_name
	add_child(root)
	auto_free(root)
	return NetwEntity.ensure(root)


func test_viewer_mutations_change_verdict() -> void:
	assert_that(layer.verdict_for(7)).is_false()
	layer.add_viewer(7)
	assert_that(layer.verdict_for(7)).is_true()

	layer.remove_viewer(7)
	assert_that(layer.verdict_for(7)).is_false()


func test_hide_from_insiders_inverts_verdict() -> void:
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	assert_that(layer.verdict_for(7)).is_true()
	layer.add_viewer(7)
	assert_that(layer.verdict_for(7)).is_false()


func test_server_peer_is_evaluated_as_an_ordinary_participant() -> void:
	assert_bool(layer.verdict_for(1)).is_false()
	layer.add_viewer(1)
	assert_bool(layer.verdict_for(1)).is_true()
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	assert_bool(layer.verdict_for(1)).is_false()


func test_entity_transitions_emit_at_service_flush() -> void:
	var mt := MultiplayerTree.new()
	mt.name = "TestTransitionTree"
	add_child(mt)
	auto_free(mt)
	var owned := mt.api.interest.layer(&"test")
	var entity := _make_entity()
	var enters: Array = []
	var exits: Array = []
	var on_enter := func(e, p): enters.append([e, p])
	var on_exit := func(e, p): exits.append([e, p])
	owned.interest_enter.connect(on_enter)
	owned.interest_exit.connect(on_exit)

	owned.add_entity(entity)
	owned.add_viewer(7)
	mt.api.interest.flush_now()

	assert_that(enters).contains_exactly([[entity, 7]])

	owned.remove_entity(entity)
	mt.api.interest.flush_now()

	assert_that(exits).contains_exactly([[entity, 7]])
	owned.interest_enter.disconnect(on_enter)
	owned.interest_exit.disconnect(on_exit)


func test_idempotent_mutations_do_not_duplicate_signals() -> void:
	var entity := _make_entity()
	var viewer_adds: Array[int] = []
	var entity_adds: Array[NetwEntity] = []
	var on_viewer_add := func(p): viewer_adds.append(p)
	var on_entity_add := func(e): entity_adds.append(e)
	layer.viewer_added.connect(on_viewer_add)
	layer.entity_added.connect(on_entity_add)

	layer.add_viewer(7)
	layer.add_viewer(7)
	layer.add_entity(entity)
	layer.add_entity(entity)

	assert_that(viewer_adds).contains_exactly([7])
	assert_that(entity_adds).contains_exactly([entity])
	layer.viewer_added.disconnect(on_viewer_add)
	layer.entity_added.disconnect(on_entity_add)


func test_layer_with_service_broadcasts_through_hooks() -> void:
	# When a layer is owned by a [NetwInterestInterface] (i.e., obtained
	# from [member NetwMultiplayer.interest]), its mutators flow through the
	# interface hooks. Without a peer the broadcast is a no-op; this just
	# verifies the layer remains usable in that mode.
	var mt := MultiplayerTree.new()
	mt.name = "TestTreeWithService"
	add_child(mt)
	auto_free(mt)

	var owned := mt.api.interest.layer(&"owned")
	var entity := _make_entity("owned_ent")
	owned.add_entity(entity)
	owned.add_viewer(11)

	assert_that(owned.has_entity(entity)).is_true()
	assert_that(owned.has_viewer(11)).is_true()
