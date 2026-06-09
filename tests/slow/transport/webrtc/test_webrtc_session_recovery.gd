## Proves [WebRTCSession] bundles ICE non-trickle and recovers a stalled join.
##
## Two raw sessions handshake over loopback ICE through a hand-wired relay the
## test perturbs: one case asserts the host signals a single answer bundle that
## carries its candidates (never separate trickle), the other swallows the first
## offer so the client must re-send. No signaler or tracker is involved, so these
## exercise the session alone.
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
	# Keep retry out of this case; we are testing the single bundle, not recovery.
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

	# Record what the host signals: it must be one answer bundle whose payload
	# carries the candidates, never a separate trickle candidate signal.
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
	# Non-trickle: the host never signals a bare candidate, and the answer it
	# does signal carries the gathered candidates inline.
	assert_bool(host_kinds.has("candidate")).is_false()
	assert_bool(host_kinds.has("answer")).is_true()
	assert_int(answer_candidates[0]).is_greater(0)
	host.close()
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
