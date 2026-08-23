## Checks for the WebRTC connect kit transports. The tracker path's real gate is
## a live pass; these cover recognition, the connect budget, ICE filtering, and
## the in-memory loopback host.
class_name TestConnectKitWebRTC
extends NetwTestSuite

func after_test() -> void:
	var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")
	session.reset()


func test_tracker_transport_recognizes_webrtc_scheme() -> void:
	var t := TrackerWebRTCTransport.new()
	var target := NetwConnectTarget.new()
	target.scheme = &"webrtc"
	assert_bool(t._can_join(target)).is_true()
	var config := NetwHostConfig.new()
	config.transport = NetwWebRTCParams.new()
	assert_bool(t._can_host(config)).is_true()
	assert_str(t._make_signaler().get_class()).is_not_empty()


func test_timeout_hint_budgets_every_retry() -> void:
	var t := TrackerWebRTCTransport.new()
	t.gather_timeout = 6.0
	t.connect_retry = 8.0
	t.max_connect_attempts = 3
	assert_float(t._timeout_hint(null)).is_equal(6.0 + 8.0 * 3.0 + 4.0)


func test_filter_ice_servers_drops_unsupported_on_native() -> void:
	var servers: Array[Dictionary] = [
		{ "urls": ["stun:stun.l.google.com:19302"] },
		{
			"urls": ["turns:openrelay.metered.ca:443"],
			"username": "u",
			"credential": "c",
		},
	]
	var filtered := WebRTCTransport.filter_ice_servers(servers)
	if OS.has_feature("web"):
		assert_int(filtered.size()).is_equal(2)
	else:
		assert_int(filtered.size()).is_equal(1)


func test_loopback_host_reaches_online() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [WebRTCLoopbackTransport.new()]

	var config := NetwHostConfig.new()
	config.transport = NetwWebRTCParams.new()
	var results: Array[NetwConnectResult] = []
	var pump := func() -> void:
		results.append(await NetwConnector.of(api).host(null, config))
	pump.call()

	var guard := 0
	while api.state != NetwMultiplayer.SessionState.ONLINE and guard < 40:
		api.poll()
		await get_tree().process_frame
		guard += 1

	assert_bool(results[0].is_ok()).is_true()
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)
	api.embedding.dispose()
