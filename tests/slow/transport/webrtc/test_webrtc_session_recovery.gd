## Proves [WebRTCSession] top-up bundles ICE and recovers a stalled join.
##
## Two raw sessions handshake over loopback ICE through a hand-wired relay the
## test perturbs: one case asserts the host signals answer bundles that carry
## candidates (never separate trickle), the other swallows the first offer so the
## client must re-send. No signaler or tracker is involved, so these exercise the
## session alone.
class_name TestWebRTCSessionRecovery
extends NetwTestSuite

const _CLIENT_ID := 1234567


# Pumps both sessions each frame until connected[0] flips or the budget elapses.
func _pump_until(
		host: WebRTCSession,
		client: WebRTCSession, \
		connected: Array,
		on_frame: Callable,
		budget: float = 20.0,
) -> void:
	var deadline := get_tree().create_timer(budget)
	var frame := 0
	while not connected[0] and deadline.time_left > 0.0:
		host.poll(0.016)
		client.poll(0.016)
		on_frame.call(frame)
		frame += 1
		await get_tree().process_frame


func test_answer_bundle_carries_candidates_and_connects() -> void:
	var host := WebRTCSession.new()
	host.ice_servers = []
	var client := WebRTCSession.new()
	client.ice_servers = []
	# Keep retry out of this case; we are testing top-up bundles, not recovery.
	client.connect_retry = 60.0

	var connected := [false]
	client.native_connected.connect(
		func(id: int) -> void:
			if id == 1:
				connected[0] = true
	)
	client.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			host.deliver(_CLIENT_ID, "client", kind, payload)
	)

	# Record what the host signals. Top-ups are offer or answer bundles whose
	# payloads carry candidates, never separate trickle candidate signals.
	var host_kinds: Array = []
	var answer_candidates := [0]
	host.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			host_kinds.append(kind)
			if kind == "answer":
				answer_candidates[0] = (payload.get("candidates", []) as Array).size()
			client.deliver(1, "host", kind, payload)
	)

	host.create_server()
	client.create_client(_CLIENT_ID)
	await _pump_until(host, client, connected, func(_f: int) -> void: pass)

	assert_bool(connected[0]).is_true()
	assert_bool(host_kinds.has("candidate")).is_false()
	assert_bool(host_kinds.has("answer")).is_true()
	assert_int(answer_candidates[0]).is_greater(0)
	host.close()
	client.close()


func test_offer_sends_immediately_then_topups_candidates() -> void:
	var client := WebRTCSession.new()
	client.ice_servers = []
	client.connect_retry = 60.0
	client.topup_interval = 0.05
	var offer_counts: Array = []
	client.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			if kind == "offer":
				offer_counts.append((payload.get("candidates", []) as Array).size())
	)

	client.create_client(_CLIENT_ID)
	var deadline := get_tree().create_timer(3.0)
	while deadline.time_left > 0.0 and offer_counts.size() < 2:
		client.poll(0.016)
		await get_tree().process_frame

	assert_int(offer_counts.size()).is_greater_equal(2)
	assert_int(int(offer_counts[0])).is_equal(0)
	assert_int(int(offer_counts[offer_counts.size() - 1])).is_greater(0)
	client.close()


func test_failed_emits_host_unresponsive_without_answer() -> void:
	var client := WebRTCSession.new()
	client.ice_servers = []
	client.connect_retry = 0.05
	client.max_connect_attempts = 1
	var reasons: Array = []
	client.failed.connect(
		func(id: int, reason: String) -> void:
			if id == 1:
				reasons.append(reason)
	)

	client.create_client(_CLIENT_ID)
	var deadline := get_tree().create_timer(2.0)
	while deadline.time_left > 0.0 and reasons.is_empty():
		client.poll(0.016)
		await get_tree().process_frame

	assert_array(reasons).is_equal(["HOST_UNRESPONSIVE"])
	client.close()


func test_dropped_first_offer_recovers_via_retry() -> void:
	var host := WebRTCSession.new()
	host.ice_servers = []
	var client := WebRTCSession.new()
	client.ice_servers = []
	# Retry quickly so the dropped attempt re-offers without dominating the
	# suite runtime. The relay widens connect_retry after forwarding recovery.
	client.connect_retry = 0.25
	client.max_connect_attempts = 3

	var connected := [false]
	client.native_connected.connect(
		func(id: int) -> void:
			if id == 1:
				connected[0] = true
	)

	# Swallow the first offer bundle entirely so the host never starts a
	# handshake and the client must re-send to recover. Forward from the second
	# offer onward.
	var dropped := [false]
	var forwarding := [false]
	client.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			if kind == "offer":
				if not dropped[0]:
					dropped[0] = true
					return
				forwarding[0] = true
				client.connect_retry = 60.0
			if forwarding[0]:
				host.deliver(_CLIENT_ID, "client", kind, payload)
	)
	host.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			client.deliver(1, "host", kind, payload)
	)

	host.create_server()
	client.create_client(_CLIENT_ID)

	await _pump_until(host, client, connected, func(_f: int) -> void: pass)
	await WebRTCTestSupport.clear_optional_sctp_reset_error()

	assert_bool(dropped[0]).is_true()
	assert_bool(connected[0]).is_true()
	host.close()
	client.close()
