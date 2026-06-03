## Unit tests for [NetwInterestLayer]. Covers the canonical mutation
## API exercised standalone (no [InterestService]) so the data model
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
	return NetwEntity.of(root)


func test_add_viewer_changes_verdict() -> void:
	assert_that(layer.verdict_for(7)).is_false()
	layer.add_viewer(7)
	assert_that(layer.verdict_for(7)).is_true()


func test_remove_viewer_changes_verdict() -> void:
	layer.add_viewer(7)
	layer.remove_viewer(7)
	assert_that(layer.verdict_for(7)).is_false()


func test_hide_from_insiders_inverts_verdict() -> void:
	layer.set_policy(NetwInterestLayer.Policy.HIDE_FROM_INSIDERS)
	assert_that(layer.verdict_for(7)).is_true()
	layer.add_viewer(7)
	assert_that(layer.verdict_for(7)).is_false()


func test_entity_add_and_drive_emits_enter() -> void:
	var entity := _make_entity()
	var enters: Array = []
	layer.interest_enter.connect(func(e, p): enters.append([e, p]))

	layer.add_entity(entity)
	layer.add_viewer(7)
	layer.drive_now([7])

	assert_that(enters).contains_exactly([[entity, 7]])


func test_remove_entity_emits_exit_for_visible_peer() -> void:
	var entity := _make_entity()
	var exits: Array = []
	layer.interest_exit.connect(func(e, p): exits.append([e, p]))

	layer.add_entity(entity)
	layer.add_viewer(7)
	layer.drive_now([7])
	layer.remove_entity(entity)

	assert_that(exits).contains_exactly([[entity, 7]])


func test_idempotent_mutations_do_not_duplicate_signals() -> void:
	var entity := _make_entity()
	var viewer_adds: Array[int] = []
	var entity_adds: Array[NetwEntity] = []
	layer.viewer_added.connect(func(p): viewer_adds.append(p))
	layer.entity_added.connect(func(e): entity_adds.append(e))

	layer.add_viewer(7)
	layer.add_viewer(7)
	layer.add_entity(entity)
	layer.add_entity(entity)

	assert_that(viewer_adds).contains_exactly([7])
	assert_that(entity_adds).contains_exactly([entity])


func test_layer_with_service_broadcasts_through_hooks() -> void:
	# When a layer is owned by an [InterestService] (i.e., obtained
	# from [NetwInterest]), its mutators flow through the service
	# hooks. Without a peer the broadcast is a no-op; this just
	# verifies the layer remains usable in that mode.
	var mt := MultiplayerTree.new()
	mt.name = "TestTreeWithService"
	add_child(mt)
	auto_free(mt)

	var owned := mt.interest.layer(&"owned")
	var entity := _make_entity("owned_ent")
	owned.add_entity(entity)
	owned.add_viewer(11)

	assert_that(owned.has_entity(entity)).is_true()
	assert_that(owned.has_viewer(11)).is_true()
