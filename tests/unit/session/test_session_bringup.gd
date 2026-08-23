## Unit tests for bring-up through [NetwConnector]: the attempt a host or join
## announces, driving a host to ONLINE, abort classification, and the deadline.
class_name TestSessionBringup
extends NetwTestSuite

func after_test() -> void:
	LocalLoopbackSession.get_shared_session().reset()


func _local_api() -> NetwMultiplayer:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [LocalTransport.new()]
	return api


func _local_config() -> NetwHostConfig:
	var config := NetwHostConfig.new()
	config.transport = NetwLocalParams.new()
	return config


func test_host_announces_one_attempt() -> void:
	var api := _local_api()
	var connector := NetwConnector.of(api)

	var started: Array[NetwConnectAttempt] = []
	connector.attempt_started.connect(func(a): started.append(a))

	var pump := func() -> void:
		await connector.host(null, _local_config())
	pump.call()

	assert_int(started.size()).is_equal(1)
	assert_object(connector.current_attempt).is_same(started[0])
	api.embedding.dispose()


func test_host_reports_one_outcome_for_the_whole_verb() -> void:
	# Progress is per attempt, an outcome is per verb call, so a caller reads
	# one result no matter how many attempts a fallback raised.
	LocalLoopbackSession.get_shared_session().reset()
	var api := _local_api()
	var connector := NetwConnector.of(api)

	var outcomes: Array[NetwConnectResult] = []
	connector.finished.connect(func(r): outcomes.append(r))

	var entered := [false]
	api.session_entered.connect(func(): entered[0] = true)

	var returned: Array[NetwConnectResult] = []
	var pump := func() -> void:
		returned.append(await connector.host(null, _local_config()))
	pump.call()

	var guard := 0
	while api.state != NetwMultiplayer.SessionState.ONLINE and guard < 40:
		api.poll()
		await get_tree().process_frame
		guard += 1

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)
	assert_bool(entered[0]).is_true()
	assert_int(outcomes.size()).is_equal(1)
	assert_bool(outcomes[0].is_ok()).is_true()
	assert_object(returned[0]).is_same(outcomes[0])
	api.embedding.dispose()


func test_abort_classifies_the_losing_result_as_cancelled() -> void:
	var api := _local_api()
	var connector := NetwConnector.of(api)

	var attempt := connector._begin_attempt()
	connector._connecting_attempt = attempt
	connector.abort()

	assert_bool(attempt.is_done()).is_true()
	assert_int(attempt.result.status) \
			.is_equal(NetwConnectResult.Status.ABORTED)
	api.embedding.dispose()


func test_a_transport_error_during_an_abort_still_reads_aborted() -> void:
	# The whole point of the classification: a transport that reports a generic
	# error on its way out must not be shown to the player as "the server
	# refused" when the player is the one who cancelled.
	var api := _local_api()
	var connector := NetwConnector.of(api)

	var attempt := connector._begin_attempt()
	connector._connecting_attempt = attempt
	connector._aborting = true
	connector._resolve_connecting(NetwConnectResult.error("socket closed"))

	assert_int(attempt.result.status) \
			.is_equal(NetwConnectResult.Status.ABORTED)
	api.embedding.dispose()


func test_the_connect_deadline_times_a_stalled_bring_up_out() -> void:
	# The deadline counts down wherever the session is pumped, so a headless
	# client with no server browser still resolves instead of hanging.
	var api := _local_api()
	var connector := NetwConnector.of(api)

	var attempt := connector._begin_attempt()
	connector._connecting_attempt = attempt
	connector._connect_deadline = 0.1

	api.poll_started.emit(0.2)

	assert_bool(attempt.is_done()).is_true()
	assert_int(attempt.result.status) \
			.is_equal(NetwConnectResult.Status.TIMED_OUT)
	api.embedding.dispose()


func test_a_self_managed_transport_never_times_out() -> void:
	# A negative deadline is the transport declaring it owns the terminal
	# outcome, so the pump must leave it alone.
	var api := _local_api()
	var connector := NetwConnector.of(api)

	var attempt := connector._begin_attempt()
	connector._connecting_attempt = attempt
	connector._connect_deadline = -1.0

	api.poll_started.emit(5.0)

	assert_bool(attempt.is_done()).is_false()
	api.embedding.dispose()


func test_a_timed_out_attempt_hands_the_machine_its_cancel_edge() -> void:
	# A dead peer left assigned would strand the machine in CONNECTING, so the
	# unwind assigns an OfflineMultiplayerPeer, the one edge every cancel takes.
	var api := _local_api()
	var connector := NetwConnector.of(api)
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(7)
	api.multiplayer_peer = peer
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.CONNECTING)

	var attempt := connector._begin_attempt()
	connector._connecting_attempt = attempt
	connector._connect_deadline = 0.1

	api.poll_started.emit(0.2)

	assert_int(attempt.result.status) \
			.is_equal(NetwConnectResult.Status.TIMED_OUT)
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.OFFLINE)
	api.embedding.dispose()
