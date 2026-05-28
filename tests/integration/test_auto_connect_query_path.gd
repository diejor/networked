## End-to-end tests for [method MultiplayerTree.auto_connect_player] driving
## the [method BackendPeer.query_server_info] decision.
##
## Verifies the host-vs-join branch on a real ENet transport: a successful
## query (live local listener) joins, anything else (timeout, unreachable,
## unsupported) hosts.
class_name TestAutoConnectQueryPath
extends NetwTestSuite


func _make_payload(username: String) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	return payload


func test_no_listener_falls_through_to_host() -> void:
	var tree := EnetTestSupport.make_client_tree(self, 29100, "_solo")
	tree.use_listen_server = true

	var err: Error = await tree.auto_connect_player(
		tree.backend, "127.0.0.1", _make_payload("alice")
	)

	assert_int(err).is_equal(OK)
	assert_int(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)
	assert_bool(tree.is_online()).is_true()

	await EnetTestSupport.stop_tree(tree)


func test_live_listener_joins_as_client() -> void:
	var host := await EnetTestSupport.start_host(self)
	assert_that(host).is_not_empty()

	var client := EnetTestSupport.make_client_tree(self, host.port, "_join")
	var err: Error = await client.auto_connect_player(
		client.backend, "127.0.0.1", _make_payload("bob")
	)

	assert_int(err).is_equal(OK)
	assert_int(client.role).is_equal(MultiplayerTree.Role.CLIENT)
	assert_bool(client.is_online()).is_true()

	await EnetTestSupport.stop_tree(client)
	await EnetTestSupport.stop_tree(host.tree)
