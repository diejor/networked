## Unit tests for [InterestComponent]. Covers the membership contract:
## spawn-property contribution, layer enrollment via
## [method NetwInterestLayer.add_entity] on tree-enter, removal on
## tree-exit, and the [member layer_ids] setter diff.
##
## A bare [MultiplayerTree] hosts each test so
## [code]Netw.ctx(self).interest[/code] resolves to a real
## [NetwInterest]. No multiplayer peer is attached.
class_name TestInterestComponent
extends NetwTestSuite

var mt: MultiplayerTree


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)


func _make_entity(entity_name: String = "Ent") -> Node:
	# Entity-root under the [MultiplayerTree] so
	# [code]Netw.ctx(self).interest[/code] resolves. No
	# [MultiplayerEntity] is attached: it would crash in [code]_ready[/code]
	# without a packed-scene template owner.
	return make_test_entity(mt, entity_name, 0, false)


func _layer(layer_id: StringName) -> NetwInterestLayer:
	return mt.interest.layer(layer_id)


func _service() -> InterestService:
	return mt.get_service(InterestService) as InterestService


func test_visibility_enter_waits_for_delayed_node() -> void:
	var layer := _layer(&"sight")
	var visible: Array[NetwEntity] = []
	var callable := func(_entity: NetwEntity): visible.append(_entity)
	layer.entity_visible.connect(callable)

	var route := 42

	_service()._rpc_visibility_events(
		[
			[route, &"sight", InterestService.Kind.ENTER],
		],
	)
	await drain_frames(get_tree(), 2)
	assert_that(visible.is_empty()).is_true()

	var root := _make_entity("Delayed")
	var entity := NetwEntity.of(root)
	
	var liveness_service := mt.get_service(LivenessService) as LivenessService
	liveness_service.bind_route(route, entity)
	
	await drain_frames(get_tree(), 2)

	assert_that(visible).contains_exactly([entity])
	layer.entity_visible.disconnect(callable)


func test_layer_configuration_flow() -> void:
	var root := _make_entity()
	var component := InterestComponent.new()
	root.add_child(component)
	var entity := NetwEntity.of(root)
	var found := false
	for c in entity._pending_spawn_props:
		if c.source == component and c.property == &"layer_ids":
			found = true
			break
	assert_that(found).is_true()

	var layer_a := _layer(&"a")
	var layer_b := _layer(&"b")
	var layer_c := _layer(&"c")
	component.layer_ids = [&"a", &"b"]
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_true()
	assert_that(layer_c.has_entity(entity)).is_false()

	component.layer_ids = [&"a", &"b", &"c"]
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_true()
	assert_that(layer_c.has_entity(entity)).is_true()

	component.layer_ids = [&"a"]
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_false()
	assert_that(layer_c.has_entity(entity)).is_false()

	component.layer_ids = [&"c"]
	assert_that(layer_a.has_entity(entity)).is_false()
	assert_that(layer_b.has_entity(entity)).is_false()
	assert_that(layer_c.has_entity(entity)).is_true()

	root.get_parent().remove_child(root)
	assert_that(layer_c.has_entity(entity)).is_false()


func test_admit_viewer_fires_interest_enter_on_entity() -> void:
	var layer := _layer(&"arena")
	var root := _make_entity("Player")
	var component := InterestComponent.new()
	component.layer_ids = [&"arena"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	var enters: Array[int] = []
	var callable := func(peer: int): enters.append(peer)
	entity.interest_enter.connect(callable)

	layer.add_viewer(7)

	assert_that(enters).contains_exactly([7])
	layer.remove_viewer(7)
	entity.interest_enter.disconnect(callable)


func test_off_tree_entity_keeps_admission_until_driven() -> void:
	var layer := _layer(&"arena")
	var root := Node.new()
	root.name = "Player"
	var entity := NetwEntity.new()
	root.set_meta(NetwEntity._META_KEY, entity)
	entity.owner = root
	auto_free(root)

	layer.add_viewer(7)
	layer.add_entity(entity)
	_service().flush()

	assert_that(_service().can_peer_see_entity(7, entity)).is_true()

	mt.add_child(root)
	_service().flush()

	assert_that(_service().can_peer_see_entity(7, entity)).is_true()
	layer.remove_entity(entity)
