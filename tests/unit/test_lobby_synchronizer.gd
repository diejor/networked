## Unit tests for LobbySynchronizer.scene_visibility_filter().
##
## scene_visibility_filter() is pure GDScript logic that reads connected_clients.
## These tests set connected_clients directly rather than going through
## connect_client() / disconnect_client(), which call set_visibility_for() — a
## C++ MultiplayerSynchronizer method that requires a real peer registered in the
## engine's replication interface. That requirement belongs in integration tests.
class_name TestLobbySynchronizer
extends NetworkedTestSuite

const SYNCHRONIZER_SCENE := preload("res://addons/networked/core/lobby/LobbySynchronizer.tscn")

var sync: LobbySynchronizer


func before_test() -> void:
	sync = SYNCHRONIZER_SCENE.instantiate()
	add_child(sync)
	auto_free(sync)


func test_unknown_peer_not_visible_by_default() -> void:
	assert_that(sync.scene_visibility_filter(99)).is_false()


func test_server_peer_always_visible() -> void:
	assert_that(sync.scene_visibility_filter(MultiplayerPeer.TARGET_PEER_SERVER)).is_true()


func test_zero_peer_not_visible() -> void:
	assert_that(sync.scene_visibility_filter(0)).is_false()


func test_registered_peer_is_visible() -> void:
	sync.connected_clients[5] = true
	assert_that(sync.scene_visibility_filter(5)).is_true()


func test_erased_peer_no_longer_visible() -> void:
	sync.connected_clients[5] = true
	sync.connected_clients.erase(5)
	assert_that(sync.scene_visibility_filter(5)).is_false()


func test_multiple_peers_independently_visible() -> void:
	sync.connected_clients[10] = true
	sync.connected_clients[20] = true
	assert_that(sync.scene_visibility_filter(10)).is_true()
	assert_that(sync.scene_visibility_filter(20)).is_true()


func test_unregistered_peer_invisible_when_others_registered() -> void:
	sync.connected_clients[10] = true
	assert_that(sync.scene_visibility_filter(99)).is_false()
