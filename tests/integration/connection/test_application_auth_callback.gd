## Verifies that application authentication composes with Networked's
## reserved probe protocol on a real transport.
class_name TestApplicationAuthCallback
extends NetwTestSuite

static var _request := PackedByteArray([0x47, 0x41, 0x4D, 0x45])
static var _response := PackedByteArray([0x4F, 0x4B])


func test_custom_auth_connects_without_consuming_probes() -> void:
	var host := await EnetTestSupport.start_host(self, Callable(), 0.2)
	assert_that(host).is_not_empty()

	var host_tree: MultiplayerTree = host.tree
	var client_tree := EnetTestSupport.make_client_tree(
		self,
		host.port,
		"ApplicationAuth",
	)
	var host_api: NetwMultiplayer = host_tree.api
	var client_api: NetwMultiplayer = client_tree.api
	var host_packets: Array[PackedByteArray] = []

	host_api.auth_callback = func(
			peer_id: int,
			data: PackedByteArray,
	) -> void:
		host_packets.append(data)
		if data == _request:
			host_api.peer_send_auth(peer_id, _response)
			host_api.peer_complete_auth(peer_id)
	client_api.auth_callback = func(
			peer_id: int,
			data: PackedByteArray,
	) -> void:
		if data == _response:
			client_api.peer_complete_auth(peer_id)
	var send_request := func(peer_id: int) -> void:
		client_api.peer_send_auth(peer_id, _request)
	client_api.peer_authenticating.connect(send_request)

	var payload := JoinPayload.new()
	payload.username = &"application-auth"
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": host.port }

	var joined := await NetwConnector.of(client_tree.api).join(target, payload, true)
	assert_bool(joined.is_ok()).is_true()
	assert_array(host_packets).is_equal([_request])
	var client_id := client_api.get_unique_id()
	@warning_ignore("redundant_await")
	await assert_func(host_api, "peer_get_participant", [client_id]) \
			.wait_until(1000) \
			.is_not_null()

	var probe_target := NetwConnectTarget.new()
	probe_target.scheme = &"enet"
	probe_target.address = "127.0.0.1"
	probe_target.metadata = { "port": host.port }
	var probe: NetwProbeResult = await NetwConnector.of(client_tree.api).probe(probe_target)
	assert_int(probe.status).is_equal(NetwProbeResult.Status.OK)
	assert_array(host_packets).is_equal([_request])

	client_api.peer_authenticating.disconnect(send_request)
	await EnetTestSupport.stop_tree(client_tree)
	await EnetTestSupport.stop_tree(host_tree)
