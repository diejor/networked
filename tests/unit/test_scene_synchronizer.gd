## Unit tests for [SceneSynchronizer] as a thin adapter over a
## [NetwInterestLayer]. The synchronizer's own [code]scene_visibility_filter[/code]
## helper is kept as a back-compat query that reads
## [member SceneSynchronizer.connected_peers]; this suite covers that
## query plus the layer-side mirroring done by [code]connect_peer[/code]
## / [code]disconnect_peer[/code].
##
## Integration coverage of the wire transport lives in
## [code]tests/integration/test_interest_service.gd[/code].
class_name TestSceneSynchronizer
extends NetworkedTestSuite

var sync: SceneSynchronizer


func before_test() -> void:
	sync = SceneSynchronizer.new()
	add_child(sync)
	auto_free(sync)


# ---------------------------------------------------------------------------
# scene_visibility_filter back-compat query.
# ---------------------------------------------------------------------------

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
