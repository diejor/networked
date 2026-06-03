## Integration tests for [method BackendPeer.query_server_info] over a real
## [ENetBackend].
##
## The harness ([NetwTestHarness]) is built around [LocalLoopbackBackend],
## which overrides [code]query_server_info[/code], so the
## [SceneMultiplayer] NPRB handshake can only be exercised on a real
## transport. These tests stand up a standalone ENet host and probe it from
## a second tree in the same process.
class_name TestQueryServerInfo
extends NetwTestSuite

func test_query_returns_ok_with_player_count() -> void:
	var host := await EnetTestSupport.start_host(self)
	assert_that(host).is_not_empty()

	var port: int = host.port
	var client_backend := EnetTestSupport.make_client_backend(port)
	var result: ServerInfoResult = await client_backend.query_server_info(
		"127.0.0.1",
		2.0,
	)

	assert_that(result).is_not_null()
	assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
	assert_that(result.info).is_not_null()
	assert_bool(result.info.is_local_listener).is_true()
	assert_int(result.info.players).is_equal(0)
	assert_int(result.latency_ms).is_greater_equal(0)

	await EnetTestSupport.stop_tree(host.tree)


func test_query_unreachable_port_does_not_return_ok() -> void:
	# Pick a port well outside the host range so nothing is listening.
	var dead_port := 29000
	var client_backend := EnetTestSupport.make_client_backend(dead_port)
	var result: ServerInfoResult = await client_backend.query_server_info(
		"127.0.0.1",
		0.5,
	)

	assert_that(result).is_not_null()
	assert_bool(result.is_ok()).is_false()
