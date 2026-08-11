## Drive checks for the simple connect kit transports ([ENetTransport],
## [WebSocketTransport], [LocalTransport]) through [NetwSessionHandle].
class_name TestConnectKitTransports
extends NetwTestSuite

func after_test() -> void:
	LocalLoopbackSession.get_shared_session().reset()


func test_transports_recognize_their_scheme() -> void:
	var enet := ENetTransport.new()
	var ws := WebSocketTransport.new()
	var local := LocalTransport.new()

	var enet_target := NetwConnectTarget.new()
	enet_target.scheme = &"enet"
	assert_bool(enet._can_join(enet_target)).is_true()
	assert_bool(ws._can_join(enet_target)).is_false()
	assert_bool(local._can_join(enet_target)).is_false()

	var local_target := NetwConnectTarget.new()
	local_target.scheme = &"local"
	assert_bool(local._can_join(local_target)).is_true()


func test_websocket_url_normalization() -> void:
	var ws := WebSocketTransport.new()
	assert_str(ws.build_url("")).is_equal("ws://localhost:21253")
	assert_str(ws.build_url("localhost")).is_equal("ws://localhost:21253")
	assert_str(ws.build_url("wss://example.com")).is_equal("wss://example.com")
	assert_str(ws.build_url("example.com")).is_equal("wss://example.com")


func test_host_via_local_transport_reaches_online() -> void:
	LocalLoopbackSession.get_shared_session().reset()
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [LocalTransport.new()]

	var config := NetwHostConfig.new()
	config.transport = NetwLocalParams.new()
	var results: Array[NetwConnectResult] = []
	var pump := func() -> void:
		results.append(await NetwConnector.of(api).host(null, config))
	pump.call()

	var guard := 0
	while api.state != SessionCore.State.ONLINE and guard < 40:
		api.poll()
		await get_tree().process_frame
		guard += 1

	assert_bool(results[0].is_ok()).is_true()
	assert_int(api.state).is_equal(SessionCore.State.ONLINE)
	assert_object(NetwConnector.of(api).peer_view).is_not_null()
	api.embedding.dispose()


func test_host_with_payload_admits_the_host_player() -> void:
	LocalLoopbackSession.get_shared_session().reset()
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [LocalTransport.new()]

	var config := NetwHostConfig.new()
	config.transport = NetwLocalParams.new()
	var payload := JoinPayload.new()
	payload.username = &"host"

	var pump := func() -> void:
		await NetwConnector.of(api).host(payload, config)
	pump.call()

	var guard := 0
	while api.peer_get_accepted_join(1) == null and guard < 60:
		api.poll()
		await get_tree().process_frame
		guard += 1

	assert_int(api.state).is_equal(SessionCore.State.ONLINE)
	# The host submitted its own join, so a roster row with an accepted join
	# exists for peer 1 without any client connecting.
	assert_object(api.peer_get_accepted_join(1)).is_not_null()
	api.embedding.dispose()


func test_join_with_no_matching_transport_errors() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [LocalTransport.new()]

	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	var result := await NetwConnector.of(api).join(target, null, true)

	assert_int(NetwConnector.error_of(result)).is_equal(ERR_CANT_CONNECT)
	assert_int(NetwConnector.of(api).current_attempt.result.status) \
			.is_equal(NetwConnectResult.Status.ERROR)
	api.embedding.dispose()
