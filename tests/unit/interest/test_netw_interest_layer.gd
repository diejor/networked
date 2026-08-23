## Unit tests for [NetwInterestLayer]. Covers the canonical mutation
## API exercised standalone (no session) so the data model
## is testable in isolation.
class_name TestNetwInterestLayer
extends NetwTestSuite

var layer: NetwInterestLayer


func before_test() -> void:
	layer = NetwInterestLayer.new()
	layer.layer_id = &"test"


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
	var owned := mt.api._native_core.interest_layer(&"test")
	var entity := _make_entity()
	var enters: Array = []
	var exits: Array = []
	var on_enter := func(e, p): enters.append([e, p])
	var on_exit := func(e, p): exits.append([e, p])
	owned.interest_enter.connect(on_enter)
	owned.interest_exit.connect(on_exit)

	owned.add_entity(entity)
	owned.add_viewer(7)
	mt.api.interest_flush()

	assert_that(enters).contains_exactly([[entity, 7]])

	owned.remove_entity(entity)
	mt.api.interest_flush()

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


func test_client_admit_dispatches_enter_before_visible() -> void:
	var entity := _make_entity("client_ent")
	var events: Array[StringName] = []
	var on_enter := func(_layer_id: StringName, _peer: int) -> void:
		events.append(&"enter")
	var on_visible := func(_entity: NetwEntity) -> void:
		events.append(&"visible")
	entity.interest.on_enter(&"test", on_enter)
	layer.entity_visible.connect(on_visible)

	layer.client_admit(entity)

	assert_bool(layer.has_entity(entity)).is_true()
	assert_that(events).contains_exactly([&"enter", &"visible"])
	layer.entity_visible.disconnect(on_visible)


func test_client_revoke_dispatches_leave_before_hidden() -> void:
	var entity := _make_entity("client_ent")
	var events: Array[StringName] = []
	var on_leave := func(_layer_id: StringName, _peer: int) -> void:
		events.append(&"leave")
	var on_hidden := func(_entity: NetwEntity) -> void:
		events.append(&"hidden")
	entity.interest.on_leave(&"test", on_leave)
	layer.entity_hidden.connect(on_hidden)
	layer.client_admit(entity)
	events.clear()

	layer.client_revoke(entity)

	assert_bool(layer.has_entity(entity)).is_false()
	assert_that(events).contains_exactly([&"leave", &"hidden"])
	layer.entity_hidden.disconnect(on_hidden)


func test_client_untrack_removes_then_dispatches_leave_and_hidden() -> void:
	var entity := _make_entity("client_ent")
	var events: Array[StringName] = []
	var on_removed := func(_entity: NetwEntity) -> void:
		events.append(&"removed")
	var on_leave := func(_layer_id: StringName, _peer: int) -> void:
		events.append(&"leave")
	var on_hidden := func(_entity: NetwEntity) -> void:
		events.append(&"hidden")
	entity.interest.on_leave(&"test", on_leave)
	layer.entity_removed.connect(on_removed)
	layer.entity_hidden.connect(on_hidden)
	layer.client_admit(entity)
	events.clear()

	layer.client_untrack_entity(entity)

	assert_bool(layer.has_entity(entity)).is_false()
	assert_that(events).contains_exactly(
		[&"removed", &"leave", &"hidden"],
	)
	layer.entity_removed.disconnect(on_removed)
	layer.entity_hidden.disconnect(on_hidden)


func test_layer_with_service_broadcasts_through_hooks() -> void:
	# When a layer is owned by a session (i.e., obtained
	# from [member NetwMultiplayer.interest]), its mutators flow through the
	# interface hooks. Without a peer the broadcast is a no-op; this just
	# verifies the layer remains usable in that mode.
	var mt := MultiplayerTree.new()
	mt.name = "TestTreeWithService"
	add_child(mt)
	auto_free(mt)

	var owned := mt.api._native_core.interest_layer(&"owned")
	var entity := _make_entity("owned_ent")
	owned.add_entity(entity)
	owned.add_viewer(11)

	assert_that(owned.has_entity(entity)).is_true()
	assert_that(owned.has_viewer(11)).is_true()
