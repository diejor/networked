## Integration tests for [method NetwConnector.probe] over a real
## ENet transport.
class_name TestProbeServerInfo
extends NetwTestSuite

func test_query_returns_ok_with_player_count() -> void:
	var host := await EnetTestSupport.start_host(self)
	assert_that(host).is_not_empty()

	var client_tree := MultiplayerTree.new()
	add_child(client_tree)

	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": host.port }

	var result: NetwProbeResult = await client_tree.connector.probe(target)

	assert_that(result).is_not_null()
	assert_int(result.status).is_equal(NetwProbeResult.Status.OK)
	assert_that(result.info).is_not_null()
	assert_bool(result.info.is_local_listener).is_true()
	assert_int(result.info.players).is_equal(0)
	assert_int(result.latency_ms).is_greater_equal(0)

	client_tree.queue_free()
	await EnetTestSupport.stop_tree(host.tree)


func test_query_unreachable_port_does_not_return_ok() -> void:
	# Pick a port well outside the host range so nothing is listening.
	var dead_port := 29000
	var client_tree := MultiplayerTree.new()
	add_child(client_tree)

	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": dead_port }

	var result: NetwProbeResult = await client_tree.connector.probe(target)

	assert_that(result).is_not_null()
	assert_bool(result.is_ok()).is_false()

	client_tree.queue_free()
