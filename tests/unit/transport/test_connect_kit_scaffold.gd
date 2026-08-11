## Scaffold checks for the connect kit types ([NetwTransport],
## [NetwPeerView], [NetwConnectAttempt], and their data types).
class_name TestConnectKitScaffold
extends NetwTestSuite

# A minimal transport that only recognizes the &"fake" scheme.
class FakeTransport:
	extends NetwTransport

	func _can_join(target: NetwConnectTarget) -> bool:
		return target != null and target.scheme == &"fake"


	func _can_host(config: NetwHostConfig) -> bool:
		return config != null and config.scheme == &"fake"


	func _display_name() -> String:
		return "Fake"


func test_connect_result_builders() -> void:
	assert_bool(NetwConnectResult.ok().is_ok()).is_true()
	assert_bool(NetwConnectResult.aborted().is_ok()).is_false()
	var r := NetwConnectResult.unreachable(&"no_relay", "down")
	assert_int(r.status).is_equal(NetwConnectResult.Status.UNREACHABLE)
	assert_str(String(r.detail)).is_equal("no_relay")


func test_address_hint_make() -> void:
	var h := NetwAddressHint.make("Room code", "abc", "help", true, true)
	assert_str(h.label).is_equal("Room code")
	assert_bool(h.accepts_empty).is_true()
	assert_bool(h.supports_probe).is_true()


func test_generic_view_degrades() -> void:
	var view := NetwPeerView.new(null)
	assert_str(view.join_address()).is_equal("")
	assert_dict(view.diagnostics(1)).is_empty()
	var progress := view.describe_progress()
	assert_str(String(progress.get("step", &""))).is_equal("disconnected")


func test_enet_view_reports_host_join_address() -> void:
	var server := ENetMultiplayerPeer.new()
	assert_int(server.create_server(0, 4)).is_equal(OK)
	var view := ENetPeerView.new(server)
	var address := view.join_address()
	var port := server.host.get_local_port()
	assert_bool(address.ends_with(":%d" % port)) \
			.override_failure_message(
				"host join_address '%s' should carry port %d" % [address, port],
			) \
			.is_true()
	view.close()
	server.close()


func test_websocket_view_reports_host_join_address() -> void:
	var server := WebSocketMultiplayerPeer.new()
	assert_int(server.create_server(38472)).is_equal(OK)
	var view := WebSocketPeerView.new(server, 38472)
	var address := view.join_address()
	assert_bool(address.begins_with("ws://") and address.ends_with(":38472")) \
			.override_failure_message(
				"host join_address '%s' should be a ws:// URL on port 38472"
				% address,
			) \
			.is_true()
	# A view with no build context has no port, so it degrades to unshareable.
	assert_str(WebSocketPeerView.new(server, 0).join_address()).is_equal("")
	view.close()
	server.close()


func test_empty_address_renders_transport_placeholder() -> void:
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	assert_str(ConnectBrowser.format_address(target)).is_equal("localhost")
	target.address = "10.0.0.5:21253"
	assert_str(ConnectBrowser.format_address(target)).is_equal("10.0.0.5:21253")


func test_probe_reply_carries_host_cap() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var config := NetwHostConfig.new()
	var enet := NetwENetParams.new()
	enet.max_clients = 8
	config.transport = enet
	# An unset config cap advertises the transport's own resolved one, which
	# ENet writes back into its params after create_server picks it.
	NetwConnector.of(api)._advertise_host(config)
	var info := NetwServerInfo.from_session(api)
	assert_int(info.max_players).is_equal(8)
	api.embedding.dispose()


func test_per_session_transport_override_resolves_by_scheme() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var fake := FakeTransport.new()
	NetwConnector.of(api).transports = [fake]

	var target := NetwConnectTarget.new()
	target.scheme = &"fake"
	assert_object(NetwConnector.of(api)._resolve_join(target)).is_same(fake)

	# The override replaces the global registry rather than extending it, so a
	# scheme the rig did not register stays unresolvable for this session.
	var other := NetwConnectTarget.new()
	other.scheme = &"enet"
	assert_object(NetwConnector.of(api)._resolve_join(other)).is_null()
	api.embedding.dispose()


func test_builtin_transports_resolve_from_the_global_registry() -> void:
	# A session with no per-session override resolves the shipped schemes
	# registered by NetwTransport._static_init.
	var api := NetwMultiplayer.new(SceneMultiplayer.new())

	var enet := NetwConnectTarget.new()
	enet.scheme = &"enet"
	assert_object(NetwConnector.of(api)._resolve_join(enet)).is_not_null()

	var local := NetwConnectTarget.new()
	local.scheme = &"local"
	assert_object(NetwConnector.of(api)._resolve_join(local)).is_not_null()

	var webrtc := NetwConnectTarget.new()
	webrtc.scheme = &"webrtc"
	assert_object(NetwConnector.of(api)._resolve_join(webrtc)).is_not_null()

	var ws := NetwHostConfig.new()
	ws.transport = NetwWebSocketParams.new()
	assert_object(NetwConnector.of(api)._resolve_host(ws)).is_not_null()
	api.embedding.dispose()


func test_global_registry_add_and_remove() -> void:
	var fake := FakeTransport.new()
	NetwTransport.register(fake)
	assert_array(NetwTransport.registered()).contains([fake])
	NetwTransport.unregister(fake)
	assert_array(NetwTransport.registered()).not_contains([fake])


func test_join_starts_an_attempt() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [FakeTransport.new()]
	var started: Array[NetwConnectAttempt] = []
	NetwConnector.of(api).attempt_started.connect(func(a): started.append(a))

	var target := NetwConnectTarget.new()
	target.scheme = &"fake"
	var pump := func() -> void:
		await NetwConnector.of(api).join(target, null, true)
	pump.call()

	assert_int(started.size()).is_equal(1)
	assert_object(NetwConnector.of(api).current_attempt).is_same(started[0])
	assert_object(started[0].target).is_same(target)
	api.embedding.dispose()


func test_attempt_abort_resolves_once() -> void:
	var attempt := NetwConnectAttempt.new()
	var results: Array[NetwConnectResult] = []
	attempt.finished.connect(func(r): results.append(r))

	attempt.abort()
	attempt.abort()

	assert_int(results.size()).is_equal(1)
	assert_int(results[0].status).is_equal(NetwConnectResult.Status.ABORTED)
	assert_bool(attempt.is_done()).is_true()
