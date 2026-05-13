## Unit tests for [SceneSynchronizer] visibility filtering.
##
## [method SceneSynchronizer.scene_visibility_filter] is pure GDScript logic
## that reads [member SceneSynchronizer.connected_peers]. These tests set
## peers directly to avoid C++ [MultiplayerSynchronizer] requirements that
## belong in integration tests.
class_name TestSceneSynchronizer
extends NetworkedTestSuite

var sync: SceneSynchronizer


func before_test() -> void:
	sync = SceneSynchronizer.new()
	add_child(sync)
	auto_free(sync)


func test_unknown_peer_not_visible_by_default() -> void:
	assert_that(sync.scene_visibility_filter(99)).is_false()


func test_server_peer_always_visible() -> void:
	assert_that(sync.scene_visibility_filter(
		MultiplayerPeer.TARGET_PEER_SERVER)).is_true()


func test_zero_peer_not_visible() -> void:
	assert_that(sync.scene_visibility_filter(0)).is_false()


func test_registered_peer_is_visible() -> void:
	sync.connected_peers[5] = true
	assert_that(sync.scene_visibility_filter(5)).is_true()


func test_erased_peer_no_longer_visible() -> void:
	sync.connected_peers[5] = true
	sync.connected_peers.erase(5)
	assert_that(sync.scene_visibility_filter(5)).is_false()


func test_multiple_peers_independently_visible() -> void:
	sync.connected_peers[10] = true
	sync.connected_peers[20] = true
	assert_that(sync.scene_visibility_filter(10)).is_true()
	assert_that(sync.scene_visibility_filter(20)).is_true()


func test_unregistered_peer_invisible_when_others_registered() -> void:
	sync.connected_peers[10] = true
	assert_that(sync.scene_visibility_filter(99)).is_false()
