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
	layer.entity_visible.connect(
		func(_entity: NetwEntity): visible.append(_entity)
	)

	_service()._rpc_visibility_events(
		[
			[NodePath("Delayed"), &"sight", InterestService.Kind.ENTER],
		],
	)
	await drain_frames(get_tree(), 2)
	assert_that(visible.is_empty()).is_true()

	var root := _make_entity("Delayed")
	var entity := NetwEntity.of(root)
	await drain_frames(get_tree(), 2)

	assert_that(visible).contains_exactly([entity])
	assert_that(_service()._pending_visibility_events.is_empty()).is_true()

# ---------------------------------------------------------------------------
# Spawn-property contribution.
# ---------------------------------------------------------------------------


func test_parented_contributes_layer_ids_property() -> void:
	var root := _make_entity()
	var component := InterestComponent.new()
	root.add_child(component)
	# With no [MultiplayerEntity] yet registered, the contribution lands
	# in the entity's pending buffer. [method NetwEntity.set_spawner]
	# flushes it to [member MultiplayerEntity.replication_config] later;
	# that flush is covered by other suites.
	var entity := NetwEntity.of(root)
	var found := false
	for c in entity._pending_spawn_props:
		if c.source == component and c.property == &"layer_ids":
			found = true
			break
	assert_that(found).is_true()

# ---------------------------------------------------------------------------
# Registration on tree-enter / tree-exit.
# ---------------------------------------------------------------------------


func test_enter_tree_registers_for_each_layer_id() -> void:
	var layer_a := _layer(&"a")
	var layer_b := _layer(&"b")
	var root := _make_entity()

	var component := InterestComponent.new()
	component.layer_ids = [&"a", &"b"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_true()


func test_exit_tree_unregisters() -> void:
	var layer_x := _layer(&"x")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"x"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	assert_that(layer_x.has_entity(entity)).is_true()

	root.get_parent().remove_child(root)
	assert_that(layer_x.has_entity(entity)).is_false()

# ---------------------------------------------------------------------------
# layer_ids setter diff.
# ---------------------------------------------------------------------------


func test_setter_adds_new_layer() -> void:
	var layer_a := _layer(&"a")
	var layer_b := _layer(&"b")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"a"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_false()

	component.layer_ids = [&"a", &"b"]
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_true()


func test_setter_removes_dropped_layer() -> void:
	var layer_a := _layer(&"a")
	var layer_b := _layer(&"b")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"a", &"b"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	component.layer_ids = [&"a"]
	assert_that(layer_a.has_entity(entity)).is_true()
	assert_that(layer_b.has_entity(entity)).is_false()


func test_setter_full_replace() -> void:
	var layer_a := _layer(&"a")
	var layer_c := _layer(&"c")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"a"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	component.layer_ids = [&"c"]
	assert_that(layer_a.has_entity(entity)).is_false()
	assert_that(layer_c.has_entity(entity)).is_true()

# ---------------------------------------------------------------------------
# End-to-end: layer + entity + viewer -> interest_enter fires.
# ---------------------------------------------------------------------------


func test_admit_viewer_fires_interest_enter_on_entity() -> void:
	var layer := _layer(&"arena")
	var root := _make_entity("Player")
	var component := InterestComponent.new()
	component.layer_ids = [&"arena"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	var enters: Array[int] = []
	entity.interest_enter.connect(
		func(peer: int): enters.append(peer)
	)

	layer.add_viewer(7)

	assert_that(enters).contains_exactly([7])
	layer.remove_viewer(7)


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
