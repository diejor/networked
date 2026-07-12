## Truth table for the one server predicate, the authority axis of the addon.
##
## There is a single question behind "may this peer author state and run
## server verbs": the native [MultiplayerAPI] server predicate, answered
## through [NetwMultiplayer]'s overridden unique-id. Offline and disconnected
## windows count as server, so a rig without a peer authors without forging a
## role. A connected client answers false. A torn-down client answers true
## again because its peer is gone. The connecting window shares the
## disconnected-peer code path with teardown, so the teardown case pins it too.
class_name TestServerPredicate
extends NetwTestSuite

var harness: NetwTestHarness


func before_test() -> void:
	harness = make_harness()
	await harness.setup()


func test_offline_tree_counts_as_server() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)
	assert_that(tree.api).is_not_null()
	assert_that(tree.api.is_server()).is_true()
	tree.queue_free()


func test_host_is_server() -> void:
	assert_that(harness.server().api.is_server()).is_true()


func test_connected_client_is_not_server() -> void:
	var client := await harness.add_client()
	assert_that(client.api.is_server()).is_false()


func test_disconnected_client_counts_as_server_again() -> void:
	var client := await harness.add_client()
	assert_that(client.api.is_server()).is_false()

	await harness.disconnect_client(client)
	assert_that(client.api.is_server()).is_true()
