## Unit tests for [NetwInterestHandle].
##
## Covers cached identity, declarative and runtime membership, lifecycle
## reattachment, callbacks, local label snapshots, and delayed relay delivery.
class_name TestInterestHandle
extends NetwTestSuite

var mt: MultiplayerTree


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)


func _make_entity(entity_name: String = "Ent") -> Node:
	return make_test_entity(mt, entity_name, 0, false)


func _layer(layer_id: StringName) -> NetwInterestLayer:
	return mt.api._interest.layer(layer_id)


func _service() -> InterestCore:
	return mt.api._interest


func test_interest_handle_is_cached() -> void:
	var entity := NetwEntity.of(_make_entity())

	assert_object(entity.interest).is_same(entity.interest)


func test_join_leave_and_tree_lifecycle() -> void:
	var root := Node.new()
	root.name = "Player"
	auto_free(root)
	var entity := NetwEntity.ensure(root)
	var interest := entity.interest
	interest.join(&"a").join(&"b").join(&"a")

	assert_array(interest.layer_ids()).contains_exactly([&"a", &"b"])
	assert_that(_layer(&"a").has_entity(entity)).is_false()

	mt.add_child(root)
	assert_that(_layer(&"a").has_entity(entity)).is_true()
	assert_that(_layer(&"b").has_entity(entity)).is_true()

	var snapshot := interest.layer_ids()
	snapshot.clear()
	assert_array(interest.layer_ids()).contains_exactly([&"a", &"b"])

	interest.leave(&"a").leave(&"a")
	assert_that(_layer(&"a").has_entity(entity)).is_false()
	assert_that(_layer(&"b").has_entity(entity)).is_true()

	mt.remove_child(root)
	assert_that(_layer(&"b").has_entity(entity)).is_false()
	mt.add_child(root)
	assert_that(_layer(&"b").has_entity(entity)).is_true()


func test_configure_interest_dispatches_layer_callbacks() -> void:
	var root := _make_entity()
	var entity := NetwEntity.of(root)
	var entered: Array = []
	var left: Array = []
	var on_enter := func(layer_id: StringName, peer_id: int):
		entered.append([layer_id, peer_id])
	var on_leave := func(layer_id: StringName, peer_id: int):
		left.append([layer_id, peer_id])
	Netw.configure_interest(root) \
			.layer(&"arena") \
			.on_enter(on_enter) \
			.on_leave(on_leave)

	var layer := _layer(&"arena")
	layer.add_viewer(7)
	_service().flush_now()
	assert_array(entered).contains_exactly([[&"arena", 7]])
	assert_that(entity.interest.is_visible_to(7)).is_true()

	layer.remove_viewer(7)
	_service().flush_now()
	assert_array(left).contains_exactly([[&"arena", 7]])
	assert_that(entity.interest.is_visible_to(7)).is_false()


func test_observer_callbacks_use_peer_only_surface() -> void:
	var entity := NetwEntity.of(_make_entity())
	var entered: Array[int] = []
	var left: Array[int] = []
	entity.interest.on_observed(func(peer_id: int): entered.append(peer_id))
	entity.interest.on_unobserved(func(peer_id: int): left.append(peer_id))

	entity.observer_entered.emit(&"sight", 7)
	entity.observer_left.emit(&"sight", 7)

	assert_array(entered).contains_exactly([7])
	assert_array(left).contains_exactly([7])
	assert_bool(entity.interest._reports_observers()).is_true()


func test_leave_policy_builder_and_custom_guard() -> void:
	var root := _make_entity()
	var entity := NetwEntity.of(root)
	Netw.configure_interest(root).layer(
		&"stealth",
		NetwMultiplayer.LeavePolicy.RETAIN,
	)

	assert_int(
		entity.interest._leave_policy_for(
			&"stealth",
			NetwMultiplayer.LeavePolicy.DESPAWN,
		),
	).is_equal(NetwMultiplayer.LeavePolicy.RETAIN)
	await assert_error(
		func() -> void:
			entity.interest.on_leave_policy(
				&"stealth",
				NetwMultiplayer.LeavePolicy.CUSTOM,
			)
	).is_runtime_error(
		"Assertion failed: NetwInterestHandle.on_leave_policy: "
		+ "CUSTOM requires a callback",
	)


func test_perception_builder_and_custom_guard() -> void:
	var root := _make_entity()
	var entity := NetwEntity.of(root)
	Netw.configure_interest(root).layer(
		&"stealth",
		NetwMultiplayer.LeavePolicy.RETAIN,
		NetwMultiplayer.PerceptionPolicy.SHOW,
	)

	assert_int(
		entity.interest._perception_policy_for(
			&"stealth",
			NetwMultiplayer.PerceptionPolicy.HIDE,
		),
	).is_equal(NetwMultiplayer.PerceptionPolicy.SHOW)
	await assert_error(
		func() -> void:
			entity.interest.on_perception_policy(
				&"stealth",
				NetwMultiplayer.PerceptionPolicy.CUSTOM,
			)
	).is_runtime_error(
		"Assertion failed: NetwInterestHandle.on_perception_policy: "
		+ "CUSTOM requires a callback",
	)


func test_wire_admission_does_not_override_host_participant_row() -> void:
	mt.api._session.role = SessionCore.Role.LISTEN_SERVER
	var entity := NetwEntity.of(_make_entity())
	var layer := _layer(&"stealth")
	layer.add_entity(entity)
	_service().flush_now()

	assert_bool(_service().wire_admits(1, entity)).is_true()
	assert_bool(_service().participant_sees(1, entity)).is_false()

	layer.add_viewer(1)
	_service().flush_now()
	assert_bool(_service().participant_sees(1, entity)).is_true()


func test_hide_perception_restores_visual_and_audio_state() -> void:
	mt.api._session.role = SessionCore.Role.LISTEN_SERVER
	var root := Node2D.new()
	root.name = "PerceptionRoot"
	var audio := AudioStreamPlayer.new()
	audio.volume_db = -12.0
	root.add_child(audio)
	mt.add_child(root)
	var entity := NetwEntity.ensure(root)
	var layer := _layer(&"stealth")
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	layer.add_viewer(1)
	layer.add_entity(entity)
	_service().flush_now()

	assert_bool(_service().wire_admits(1, entity)).is_true()
	assert_bool(_service().participant_sees(1, entity)).is_false()
	assert_bool(root.visible).is_false()
	assert_float(audio.volume_db).is_equal(-80.0)

	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS)
	_service().flush_now()
	assert_bool(_service().participant_sees(1, entity)).is_true()
	assert_bool(root.visible).is_true()
	assert_float(audio.volume_db).is_equal(-12.0)


func test_custom_perception_receives_both_local_edges() -> void:
	mt.api._session.role = SessionCore.Role.LISTEN_SERVER
	var root := Node2D.new()
	root.name = "CustomPerceptionRoot"
	mt.add_child(root)
	var entity := NetwEntity.ensure(root)
	var events: Array = []
	entity.interest.on_perception_policy(
		&"stealth",
		NetwMultiplayer.PerceptionPolicy.CUSTOM,
		func(visible: bool, peer_id: int, layer_id: StringName):
			events.append([visible, peer_id, layer_id]),
	)
	var layer := _layer(&"stealth")
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	layer.add_viewer(1)
	layer.add_entity(entity)
	_service().flush_now()

	assert_array(events).contains_exactly([[false, 1, &"stealth"]])
	assert_bool(root.visible).is_true()

	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS)
	_service().flush_now()
	assert_array(events).contains_exactly(
		[[false, 1, &"stealth"], [true, 1, &"stealth"]],
	)


func test_awareness_enter_waits_for_delayed_node() -> void:
	var layer := _layer(&"sight")
	var visible: Array[NetwEntity] = []
	var callable := func(entity: NetwEntity): visible.append(entity)
	layer.entity_visible.connect(callable)
	var route := 42

	_service()._handle_awareness_events(
		var_to_bytes(
			[
				[
					InterestCore.AwarenessType.LAYER,
					route,
					&"sight",
					0,
					InterestCore.Kind.ENTER,
				],
			],
		),
		1,
	)
	await drain_frames(get_tree(), 2)
	assert_that(visible.is_empty()).is_true()

	var root := _make_entity("Delayed")
	var entity := NetwEntity.of(root)
	var entered: Array = []
	entity.interest.on_enter(
		&"sight",
		func(layer_id: StringName, peer_id: int):
			entered.append([layer_id, peer_id])
	)
	mt.api._liveness.bind_route(route, entity)
	await drain_frames(get_tree(), 2)

	assert_array(visible).contains_exactly([entity])
	assert_array(entered).contains_exactly([[&"sight", 1]])
	assert_array(entity.interest.layer_ids()).contains_exactly([&"sight"])
	layer.entity_visible.disconnect(callable)


func test_observer_awareness_resolves_route_and_rejects_paths() -> void:
	var root := _make_entity("Observed")
	var entity := NetwEntity.of(root)
	var route := 43
	mt.api._liveness.bind_route(route, entity)
	var entered: Array = []
	var left: Array = []
	entity.observer_entered.connect(
		func(layer_id: StringName, peer_id: int):
			entered.append([layer_id, peer_id])
	)
	entity.observer_left.connect(
		func(layer_id: StringName, peer_id: int):
			left.append([layer_id, peer_id])
	)

	_service()._handle_awareness_events(
		var_to_bytes(
			[
				[
					InterestCore.AwarenessType.OBSERVER,
					route,
					&"sight",
					7,
					InterestCore.Kind.ENTER,
				],
				[
					InterestCore.AwarenessType.OBSERVER,
					^"Observed",
					&"sight",
					9,
					InterestCore.Kind.ENTER,
				],
			],
		),
		1,
	)
	await drain_frames(get_tree(), 2)
	assert_array(entered).contains_exactly([[&"sight", 7]])

	_service()._handle_awareness_events(
		var_to_bytes(
			[
				[
					InterestCore.AwarenessType.OBSERVER,
					route,
					&"sight",
					7,
					InterestCore.Kind.EXIT,
				],
			],
		),
		1,
	)
	await drain_frames(get_tree(), 2)
	assert_array(left).contains_exactly([[&"sight", 7]])


func test_off_tree_entity_keeps_admission_until_driven() -> void:
	var layer := _layer(&"arena")
	var root := Node.new()
	root.name = "Player"
	var entity := NetwEntity.ensure(root)
	auto_free(root)

	layer.add_viewer(7)
	layer.add_entity(entity)
	_service().flush()
	assert_that(_service().participant_sees(7, entity)).is_true()

	mt.add_child(root)
	_service().flush()
	assert_that(_service().participant_sees(7, entity)).is_true()
	layer.remove_entity(entity)
