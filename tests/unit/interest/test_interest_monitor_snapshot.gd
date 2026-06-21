## Unit tests for the interest occupancy accessors read by [InterestMonitor].
class_name TestInterestMonitorSnapshot
extends NetwTestSuite

var mt: MultiplayerTree
var service: InterestService


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.get_service(InterestService) as InterestService


func test_layer_snapshot_counts_viewers_entities_edges_transitions() -> void:
	var layer := NetwInterestLayer.new(&"sight")
	layer.add_viewer(7)
	var entity := NetwEntity.of(make_test_entity(mt, "Target", 0, false))
	layer.add_entity(entity)

	# No drive yet: occupancy and churn are zero.
	var before := layer.monitor_snapshot()
	assert_that(before[&"viewers"]).is_equal(1)
	assert_that(before[&"entities"]).is_equal(1)
	assert_that(before[&"visible_edges"]).is_equal(0)
	assert_that(before[&"transitions_total"]).is_equal(0)

	layer.drive_now([7])

	var after := layer.monitor_snapshot()
	assert_that(after[&"visible_edges"]).is_equal(1)
	assert_that(after[&"transitions_total"]).is_equal(1)


func test_visible_edge_count_drops_on_forget() -> void:
	var layer := NetwInterestLayer.new(&"sight")
	layer.add_viewer(7)
	var entity := NetwEntity.of(make_test_entity(mt, "Target", 0, false))
	layer.add_entity(entity)
	layer.drive_now([7])
	assert_that(layer.driver.visible_edge_count()).is_equal(1)

	layer.remove_entity(entity)
	assert_that(layer.driver.visible_edge_count()).is_equal(0)


func test_service_snapshot_aggregates_tree_wide() -> void:
	var layer := service.layer_for(&"sight")
	layer.add_viewer(7)
	var entity := NetwEntity.of(make_test_entity(mt, "Target", 0, false))
	# add_entity auto-drives on the server path, populating the admit matrix.
	layer.add_entity(entity)

	var snap := service.monitor_snapshot()
	assert_that(snap[&"layers"]).is_greater_equal(1)
	assert_that(snap[&"entities_filtered"]).is_equal(1)
	# The viewer plus the always-admitted server peer are both visible edges. The
	# tree-wide sum must equal the single layer's own count.
	assert_that(snap[&"visible_edges"]).is_greater_equal(1)
	assert_that(snap[&"visible_edges"]).is_equal(layer.driver.visible_edge_count())
	assert_that(snap[&"transitions_total"]).is_greater_equal(1)
