## Integration tests for [NetwTestHarness] client disconnect helpers.
class_name TestHarnessDisconnect
extends NetwTestSuite

var harness: NetwTestHarness
var client: MultiplayerTree


func before_test() -> void:
	harness = make_harness()
	await harness.setup()
	client = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func test_disconnect_client_unregisters_peer() -> void:
	var peer_id := client.multiplayer_peer.get_unique_id()

	await harness.disconnect_client(client)

	assert_that(client.is_online()).is_false()
	assert_that(peer_id in harness.server().multiplayer_api.get_peers()) \
			.is_false()


func test_reconnect_client_registers_new_peer() -> void:
	var old_peer_id := client.multiplayer_peer.get_unique_id()
	await harness.disconnect_client(client)

	await harness.reconnect_client(client)

	var new_peer_id := client.multiplayer_peer.get_unique_id()
	assert_that(client.is_online()).is_true()
	assert_that(new_peer_id).is_not_equal(old_peer_id)
	assert_that(new_peer_id in harness.server().multiplayer_api.get_peers()) \
			.is_true()
