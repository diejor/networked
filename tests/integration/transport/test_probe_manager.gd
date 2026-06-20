@tool
## Integration tests for [ConnectSession] probing over a real ENet transport.
##
## The manager wraps [method BackendPeer.probe_server_info], which is
## only meaningful on a SceneMultiplayer-driven backend ([NetwTestHarness]
## stays loopback-only by session decision).
class_name TestProbeManager
extends NetwTestSuite

const _QUERY_COUNT := 10
const _CONCURRENCY_CAP := 3


class _CountingSource:
	extends ServerDescriptor
	var advertised_players: int = 0


	func build_server_info(_tree: MultiplayerTree) -> ServerDescriptor.Info:
		var info := ServerDescriptor.Info.new()
		info.players = advertised_players
		info.max_players = 16
		info.is_local_listener = true
		return info


func _make_target(port: int) -> JoinTarget:
	var t := JoinTarget.new()
	t.display_name = "probe-target-%d" % port
	t.address = "127.0.0.1"
	var backend := ENetBackend.new()
	backend.port = port
	t.backend = backend
	return t


func test_concurrent_queries_respect_cap_and_complete_ok() -> void:
	var source := _CountingSource.new()
	source.advertised_players = 7
	var host := await EnetTestSupport.start_host(self, source, 0.2)
	assert_that(host).is_not_empty()

	var session := ConnectSession.new()
	add_child(session)
	var manager := session._probes
	manager.max_concurrent = _CONCURRENCY_CAP
	manager.default_timeout = 2.5

	var target := _make_target(host.port)
	var results: Array[BackendPeer.ProbeResult] = []
	var on_done := func(r: BackendPeer.ProbeResult) -> void:
		results.append(r)

	for i in _QUERY_COUNT:
		manager.query(target, on_done)

	var max_observed_active: int = 0
	while results.size() < _QUERY_COUNT:
		max_observed_active = max(max_observed_active, manager.active_count())
		await get_tree().process_frame

	assert_int(results.size()).is_equal(_QUERY_COUNT)
	assert_int(max_observed_active).is_less_equal(_CONCURRENCY_CAP)
	for r in results:
		assert_int(r.status).is_equal(BackendPeer.ProbeResult.Status.OK)
		assert_that(r.info).is_not_null()
		assert_int(r.info.players).is_equal(7)

	session.queue_free()
	await EnetTestSupport.stop_tree(host.tree)


func test_cancel_all_suppresses_callbacks() -> void:
	var host := await EnetTestSupport.start_host(self, null, 0.2)
	assert_that(host).is_not_empty()

	var session := ConnectSession.new()
	add_child(session)
	var manager := session._probes
	manager.max_concurrent = 2
	manager.default_timeout = 2.0

	var target := _make_target(host.port)
	var fired := [0]
	var on_done := func(_r: BackendPeer.ProbeResult) -> void:
		fired[0] += 1

	for i in 6:
		manager.query(target, on_done)

	manager.cancel_all()
	assert_int(manager.queued_count()).is_equal(0)

	# Drain enough frames for any in-flight probe_server_info to finish
	# its internal teardown. With timeout=2.0 the inner coroutine will
	# resolve on its own; we just need to confirm no callback fires.
	for i in 180:
		await get_tree().process_frame
		if manager.active_count() == 0:
			break

	assert_int(manager.active_count()).is_equal(0)
	assert_int(fired[0]).is_equal(0)

	session.queue_free()
	await EnetTestSupport.stop_tree(host.tree)
