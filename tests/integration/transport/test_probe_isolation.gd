## Verifies that NPRB probes never enter the server's [code]get_peers()[/code]
## or trigger [signal SceneMultiplayer.peer_connected].
##
## This is the load-bearing invariant that justifies running probes through
## the auth phase rather than on a sidecar port.
class_name TestProbeIsolation
extends NetwTestSuite

var _connected_peers: Array[int] = []


func _on_peer_connected(peer_id: int) -> void:
	_connected_peers.append(peer_id)


# Wraps Dictionary access for assert_func polling.
func _pending_count(state: Dictionary) -> int:
	return state.get("pending", 0)


func test_probes_do_not_register_peers() -> void:
	var host := await EnetTestSupport.start_host(self, null, 0.2)
	assert_that(host).is_not_empty()

	var host_tree: MultiplayerTree = host.tree
	var host_api := host_tree.api
	host_api.peer_connected.connect(_on_peer_connected)
	monitor_signals(host_api, false)

	var client_backend := EnetTestSupport.make_client_backend(host.port)
	for i in 5:
		var result: ServerInfoResult = await client_backend.query_server_info(
			"127.0.0.1",
			1.0,
		)
		assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
		assert_array(host_api.get_peers()).is_empty()

	@warning_ignore("redundant_await")
	await assert_signal(host_api) \
			.wait_until(300) \
			.is_not_emitted("peer_connected", [any()])

	assert_array(_connected_peers).is_empty()
	assert_array(host_api.get_peers()).is_empty()

	host_api.peer_connected.disconnect(_on_peer_connected)
	await EnetTestSupport.stop_tree(host_tree)


# Helper method to run a single probe query as a regular method. This avoids
# GDScript lambda capture/closure GC bugs with concurrent asynchronous awaits.
func _run_probe(backend: ENetBackend, results: Array, state: Dictionary) -> void:
	var r: ServerInfoResult = await backend.query_server_info("127.0.0.1", 2.0)
	results.append(r)
	state.pending -= 1


func test_concurrent_probes_drain_and_some_return_busy() -> void:
	var host := await EnetTestSupport.start_host(self, null, 0.2)
	assert_that(host).is_not_empty()

	var host_tree: MultiplayerTree = host.tree
	var host_api := host_tree.api

	var client_backend := EnetTestSupport.make_client_backend(host.port)
	var probe_count := 20
	var results: Array[ServerInfoResult] = []
	var state := { pending = probe_count }

	for i in probe_count:
		_run_probe(client_backend, results, state)
		await get_tree().process_frame

	@warning_ignore("redundant_await")
	await assert_func(self, "_pending_count", [state]) \
			.wait_until(8000) \
			.is_equal(0)

	var ok_count := 0
	var busy_count := 0
	for r in results:
		match r.status:
			ServerInfoResult.Status.OK:
				ok_count += 1
			ServerInfoResult.Status.BUSY:
				busy_count += 1
	# Beyond the rate window, the rest are answered BUSY.
	assert_int(ok_count).is_greater(0)
	assert_int(busy_count).is_greater(0)
	assert_int(ok_count + busy_count).is_equal(probe_count)

	# Pending peers drain once clients close + auth_timeout reaps stragglers.
	@warning_ignore("redundant_await")
	await assert_func(host_api, "get_authenticating_peers") \
			.wait_until(int((host_api.auth_timeout + 1.0) * 1000)) \
			.is_equal(PackedInt32Array())
	assert_array(host_api.get_authenticating_peers()).is_empty()
	assert_array(host_api.get_peers()).is_empty()

	await EnetTestSupport.stop_tree(host_tree)
