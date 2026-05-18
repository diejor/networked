## Unit tests for [InterestComponent]. Covers the membership contract:
## spawn-property contribution, [method NetwInterest.register_entity_for_layer]
## on tree-enter, unregister on tree-exit, and the [member layer_ids]
## setter diff.
##
## A bare [MultiplayerTree] hosts each test so [code]Netw.ctx(self).interest[/code]
## resolves to a real [NetwInterest]. No multiplayer peer is attached;
## anchors created here run with [code]_is_server == true[/code].
class_name TestInterestComponent
extends NetworkedTestSuite


var mt: MultiplayerTree


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)


func _make_entity(entity_name: String = "Ent") -> Node:
	# Entity-root under the [MultiplayerTree] so
	# [code]Netw.ctx(self).interest[/code] resolves. No
	# [SpawnerComponent] is attached: it would crash in [code]_ready[/code]
	# without a packed-scene template owner.
	return make_test_entity(mt, entity_name, 0, false)


func _make_anchor(layer_id: StringName) -> InterestSynchronizer:
	# Anchors must sit under [code]mt[/code] so they register with
	# [member NetwInterest._anchors] on [code]_enter_tree[/code].
	var anchor := InterestSynchronizer.new()
	anchor.name = "Anchor_" + str(layer_id)
	anchor.layer_id = layer_id
	mt.add_child(anchor)
	auto_free(anchor)
	return anchor


# ---------------------------------------------------------------------------
# Spawn-property contribution.
# ---------------------------------------------------------------------------

func test_parented_contributes_layer_ids_property() -> void:
	var root := _make_entity()
	var component := InterestComponent.new()
	root.add_child(component)
	# With no [SpawnerComponent] yet registered, the contribution lands
	# in the entity's pending buffer. [method NetwEntity.set_spawner]
	# flushes it to [member SpawnerComponent.replication_config] later;
	# that flush is covered by other suites.
	var entity := NetwEntity.of(root)
	var path := NodePath("InterestComponent:layer_ids")
	assert_that(entity._pending_spawn_props.has(path)).is_true()


# ---------------------------------------------------------------------------
# Registration on tree-enter / tree-exit.
# ---------------------------------------------------------------------------

func test_enter_tree_registers_for_each_layer_id() -> void:
	var anchor_a := _make_anchor(&"a")
	var anchor_b := _make_anchor(&"b")
	var root := _make_entity()

	var component := InterestComponent.new()
	component.layer_ids = [&"a", &"b"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	assert_that(anchor_a.has_entity(entity)).is_true()
	assert_that(anchor_b.has_entity(entity)).is_true()


func test_enter_tree_with_no_anchor_yet_queues_pending() -> void:
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"late"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	# No anchor for "late" yet -> entity sits in the pending queue.
	var anchor := _make_anchor(&"late")
	# Anchor registration drains the pending queue.
	assert_that(anchor.has_entity(entity)).is_true()


func test_exit_tree_unregisters() -> void:
	var anchor := _make_anchor(&"x")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"x"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	assert_that(anchor.has_entity(entity)).is_true()

	root.get_parent().remove_child(root)
	assert_that(anchor.has_entity(entity)).is_false()


# ---------------------------------------------------------------------------
# layer_ids setter diff.
# ---------------------------------------------------------------------------

func test_setter_adds_new_layer() -> void:
	var anchor_a := _make_anchor(&"a")
	var anchor_b := _make_anchor(&"b")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"a"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	assert_that(anchor_a.has_entity(entity)).is_true()
	assert_that(anchor_b.has_entity(entity)).is_false()

	component.layer_ids = [&"a", &"b"]
	assert_that(anchor_a.has_entity(entity)).is_true()
	assert_that(anchor_b.has_entity(entity)).is_true()


func test_setter_removes_dropped_layer() -> void:
	var anchor_a := _make_anchor(&"a")
	var anchor_b := _make_anchor(&"b")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"a", &"b"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	component.layer_ids = [&"a"]
	assert_that(anchor_a.has_entity(entity)).is_true()
	assert_that(anchor_b.has_entity(entity)).is_false()


func test_setter_full_replace() -> void:
	var anchor_a := _make_anchor(&"a")
	var anchor_c := _make_anchor(&"c")
	var root := _make_entity()
	var component := InterestComponent.new()
	component.layer_ids = [&"a"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	component.layer_ids = [&"c"]
	assert_that(anchor_a.has_entity(entity)).is_false()
	assert_that(anchor_c.has_entity(entity)).is_true()


# ---------------------------------------------------------------------------
# End-to-end: anchor + entity + viewer -> interest_enter fires.
# ---------------------------------------------------------------------------

func test_admit_viewer_fires_interest_enter_on_entity() -> void:
	var anchor := _make_anchor(&"arena")
	var root := _make_entity("Player")
	var component := InterestComponent.new()
	component.layer_ids = [&"arena"]
	root.add_child(component)

	var entity := NetwEntity.of(root)
	var enters: Array[int] = []
	entity.interest_enter.connect(
			func(peer: int): enters.append(peer))

	anchor.add_viewer(7)
	anchor.drive_now()

	assert_that(enters).contains_exactly([7])
