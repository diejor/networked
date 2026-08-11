## Unit tests for the interest occupancy accessors read by [InterestMonitor].
class_name TestInterestMonitorSnapshot
extends NetwTestSuite

var mt: MultiplayerTree
var service: InterestCore


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.api._interest


func test_layer_snapshot_counts_viewers_entities_edges_transitions() -> void:
	var layer := service.layer(&"sight")
	layer.add_viewer(7)
	var entity := NetwEntity.of(make_test_entity(mt, "Target", 0, false))
	layer.add_entity(entity)

	# No flush yet: occupancy and churn are zero.
	var _before := layer.monitor_snapshot()
	assert_that(_before[&"viewers"]).is_equal(1)
	assert_that(_before[&"entities"]).is_equal(1)
	assert_that(_before[&"visible_edges"]).is_equal(0)
	assert_that(_before[&"transitions_total"]).is_equal(0)

	service.flush_now()

	var _after := layer.monitor_snapshot()
	assert_that(_after[&"visible_edges"]).is_equal(1)
	assert_that(_after[&"transitions_total"]).is_equal(1)


func test_visible_edge_count_drops_on_forget() -> void:
	var layer := service.layer(&"sight")
	layer.add_viewer(7)
	var entity := NetwEntity.of(make_test_entity(mt, "Target", 0, false))
	layer.add_entity(entity)
	service.flush_now()
	assert_that(layer.monitor_snapshot()[&"visible_edges"]).is_equal(1)

	layer.remove_entity(entity)
	service.flush_now()
	assert_that(layer.monitor_snapshot()[&"visible_edges"]).is_equal(0)


func test_service_snapshot_aggregates_tree_wide() -> void:
	var layer := service.layer_for(&"sight")
	layer.add_viewer(7)
	var entity := NetwEntity.of(make_test_entity(mt, "Target", 0, false))
	layer.add_entity(entity)
	service.flush_now()

	var snap := service.monitor_snapshot()
	assert_that(snap[&"layers"]).is_greater_equal(1)
	assert_that(snap[&"entities_filtered"]).is_equal(1)
	assert_that(snap[&"visible_edges"]).is_equal(1)
	assert_that(snap[&"visible_edges"]).is_equal(
		layer.monitor_snapshot()[&"visible_edges"],
	)
	assert_that(snap[&"transitions_total"]).is_greater_equal(1)
