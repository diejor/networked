## Drive checks for the simple connect kit transports ([ENetTransport],
## [WebSocketTransport], [LocalTransport]) through [NetwConnector].
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
	var connector := NetwConnector.new(api)
	connector.transports = [LocalTransport.new()]

	var config := NetwHostConfig.new()
	config.scheme = &"local"
	var attempt := connector.host(config)

	var guard := 0
	while not attempt.is_done() and guard < 40:
		connector.poll(0.05)
		await get_tree().process_frame
		guard += 1

	assert_bool(attempt.is_done()).is_true()
	assert_bool(attempt.result.is_ok()).is_true()
	assert_int(api.state).is_equal(NetwSessionInterface.State.ONLINE)
	assert_object(connector.peer_view).is_not_null()
	api.dispose()


func test_host_with_payload_admits_the_host_player() -> void:
	LocalLoopbackSession.get_shared_session().reset()
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var connector := NetwConnector.new(api)
	connector.transports = [LocalTransport.new()]

	var config := NetwHostConfig.new()
	config.scheme = &"local"
	var payload := JoinPayload.new()
	payload.username = &"host"

	var attempt := connector.host(config, payload)

	var guard := 0
	while (api.get_accepted_join(1) == null or not attempt.is_done()) and guard < 60:
		connector.poll(0.05)
		await get_tree().process_frame
		guard += 1

	assert_int(api.state).is_equal(NetwSessionInterface.State.ONLINE)
	# The host submitted its own join, so a roster row with an accepted join
	# exists for peer 1 without any client connecting.
	assert_object(api.get_accepted_join(1)).is_not_null()
	api.dispose()


func test_join_with_no_matching_transport_errors() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var connector := NetwConnector.new(api)
	connector.transports = [LocalTransport.new()]

	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	var attempt := connector.join(target)
	if not attempt.is_done():
		await attempt.finished

	assert_int(attempt.result.status).is_equal(NetwConnectResult.Status.ERROR)
	api.dispose()
