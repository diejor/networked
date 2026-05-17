## Integration coverage for [InterestSynchronizer]'s deep-first hide
## ordering. Builds a parent + child entity (each with its own
## [MultiplayerSynchronizer]) enrolled in one anchor under live
## peers, admits a viewer, then removes it and asserts:
##
## [br]- [signal InterestSynchronizer.interest_exit] fires for the
##   deeper entity before the shallower one.
## [br]- Godot issue #68508 does not trigger (no
##   [code]peers_info[/code] assertion) while applying hides.
##
## The anchor sits in a container outside the [MultiplayerTree] sub-
## tree so Godot's replication path does not try to spawn the test
## fixture across peers; mutator visibility is the only thing under
## test.
class_name TestInterestSynchronizerOrdering
extends NetworkedTestSuite


var harness: NetworkTestHarness
var container: Node
var sync: InterestSynchronizer
var parent_entity: NetwEntity
var child_entity: NetwEntity
var client0: MultiplayerTree


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(null)
	client0 = await harness.add_client()

	container = Node.new()
	container.name = "OrderingContainer"
	add_child(container)
	auto_free(container)

	sync = InterestSynchronizer.new()
	sync.name = "OrderingAnchor"
	sync.layer_id = &"order"
	container.add_child(sync)

	parent_entity = _build_entity_at(container, "Parent")
	child_entity = _build_entity_at(parent_entity.owner, "Child")


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func _id0() -> int:
	return client0.multiplayer_peer.get_unique_id()


func _build_entity_at(parent: Node, entity_name: String) -> NetwEntity:
	var root := Node.new()
	root.name = entity_name
	parent.add_child(root)
	var sync_node := MultiplayerSynchronizer.new()
	sync_node.name = "Sync"
	root.add_child(sync_node)
	return NetwEntity.of(root)


# ---------------------------------------------------------------------------
# Deep-first ordering: child must fire interest_exit before parent.
# ---------------------------------------------------------------------------

func test_hide_order_is_deep_first() -> void:
	sync.add_entity(parent_entity)
	sync.add_entity(child_entity)
	sync.add_viewer(_id0())
	sync.drive_now()

	var exit_order: Array[NetwEntity] = []
	sync.interest_exit.connect(func(entity: NetwEntity, _peer: int):
		exit_order.append(entity))

	sync.remove_viewer(_id0())
	sync.drive_now()

	assert_that(exit_order.size()).is_equal(2)
	# Child is deeper in the tree, so its sync path has a greater
	# name_count -> deep-first sort emits it before parent.
	assert_that(exit_order[0]).is_equal(child_entity)
	assert_that(exit_order[1]).is_equal(parent_entity)


func test_show_order_is_shallow_first() -> void:
	sync.add_entity(parent_entity)
	sync.add_entity(child_entity)

	var enter_order: Array[NetwEntity] = []
	sync.interest_enter.connect(func(entity: NetwEntity, _peer: int):
		enter_order.append(entity))

	sync.add_viewer(_id0())
	sync.drive_now()

	assert_that(enter_order.size()).is_equal(2)
	# Shows go shallow-first to mirror the hide-deep-first invariant.
	assert_that(enter_order[0]).is_equal(parent_entity)
	assert_that(enter_order[1]).is_equal(child_entity)


# ---------------------------------------------------------------------------
# Cache reflects nested visibility correctly.
# ---------------------------------------------------------------------------

func test_both_entities_visible_after_admit() -> void:
	sync.add_entity(parent_entity)
	sync.add_entity(child_entity)
	sync.add_viewer(_id0())
	sync.drive_now()

	assert_that(sync.is_visible_to(parent_entity, _id0())).is_true()
	assert_that(sync.is_visible_to(child_entity, _id0())).is_true()


func test_both_entities_hidden_after_remove() -> void:
	sync.add_entity(parent_entity)
	sync.add_entity(child_entity)
	sync.add_viewer(_id0())
	sync.drive_now()
	sync.remove_viewer(_id0())
	sync.drive_now()

	assert_that(sync.is_visible_to(parent_entity, _id0())).is_false()
	assert_that(sync.is_visible_to(child_entity, _id0())).is_false()


# ---------------------------------------------------------------------------
# Entity-level signals also respect the order.
# ---------------------------------------------------------------------------

func test_entity_level_exit_fires_for_both() -> void:
	sync.add_entity(parent_entity)
	sync.add_entity(child_entity)
	sync.add_viewer(_id0())
	sync.drive_now()

	var parent_exits := 0
	var child_exits := 0
	parent_entity.interest_exit.connect(func(_p): parent_exits += 1)
	child_entity.interest_exit.connect(func(_p): child_exits += 1)

	sync.remove_viewer(_id0())
	sync.drive_now()

	assert_that(parent_exits).is_equal(1)
	assert_that(child_exits).is_equal(1)
