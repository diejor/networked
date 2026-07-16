## Scaffold checks for the connect kit types ([NetwConnector], [NetwTransport],
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


func test_per_instance_transport_override_resolves_by_scheme() -> void:
	var connector := NetwConnector.new(null)
	var fake := FakeTransport.new()
	connector.transports = [fake]

	var target := NetwConnectTarget.new()
	target.scheme = &"fake"
	assert_object(connector._resolve_join(target)).is_same(fake)

	var other := NetwConnectTarget.new()
	other.scheme = &"enet"
	assert_object(connector._resolve_join(other)).is_null()


func test_builtin_transports_resolve_from_the_global_registry() -> void:
	# A bare connector with no per-instance override resolves the shipped schemes
	# registered by NetwConnector._static_init.
	var connector := NetwConnector.new(null)

	var enet := NetwConnectTarget.new()
	enet.scheme = &"enet"
	assert_object(connector._resolve_join(enet)).is_not_null()

	var local := NetwConnectTarget.new()
	local.scheme = &"local"
	assert_object(connector._resolve_join(local)).is_not_null()

	var webrtc := NetwConnectTarget.new()
	webrtc.scheme = &"webrtc"
	assert_object(connector._resolve_join(webrtc)).is_not_null()

	var ws := NetwHostConfig.new()
	ws.scheme = &"ws"
	assert_object(connector._resolve_host(ws)).is_not_null()


func test_global_registry_add_and_remove() -> void:
	var fake := FakeTransport.new()
	NetwConnector.add_transport(fake)
	assert_array(NetwConnector.get_transports()).contains([fake])
	NetwConnector.remove_transport(fake)
	assert_array(NetwConnector.get_transports()).not_contains([fake])


func test_join_starts_an_attempt() -> void:
	var connector := NetwConnector.new(null)
	var started: Array[NetwConnectAttempt] = []
	connector.attempt_started.connect(func(a): started.append(a))

	var target := NetwConnectTarget.new()
	target.scheme = &"fake"
	var attempt := connector.join(target)

	assert_object(attempt).is_not_null()
	assert_object(connector.current_attempt).is_same(attempt)
	assert_int(started.size()).is_equal(1)
	assert_object(attempt.target).is_same(target)


func test_attempt_abort_resolves_once() -> void:
	var attempt := NetwConnectAttempt.new()
	var results: Array[NetwConnectResult] = []
	attempt.finished.connect(func(r): results.append(r))

	attempt.abort()
	attempt.abort()

	assert_int(results.size()).is_equal(1)
	assert_int(results[0].status).is_equal(NetwConnectResult.Status.ABORTED)
	assert_bool(attempt.is_done()).is_true()
