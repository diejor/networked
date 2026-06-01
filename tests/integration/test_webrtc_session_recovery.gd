## Proves [WebRTCSession] tolerates out-of-order ICE and recovers a stalled join.
##
## Two raw sessions handshake over loopback ICE through a hand-wired relay the
## test perturbs: one case delivers candidates before the answer that anchors
## them, the other swallows the first answer so the client must re-offer. No
## signaler or tracker is involved, so these exercise the session alone.
class_name TestWebRTCSessionRecovery
extends NetwTestSuite

const _CLIENT_ID := 1234567


# Pumps both sessions each frame until connected[0] flips or the budget elapses.
func _pump_until(host: WebRTCSession, client: WebRTCSession, \
		connected: Array, on_frame: Callable, budget: float = 20.0) -> void:
	var deadline := get_tree().create_timer(budget)
	var frame := 0
	while not connected[0] and deadline.time_left > 0.0:
		host.poll(0.016)
		client.poll(0.016)
		on_frame.call(frame)
		frame += 1
		await get_tree().process_frame


func test_candidate_before_description_still_connects() -> void:
	var host := WebRTCSession.new()
	host.ice_servers = []
	var client := WebRTCSession.new()
	client.ice_servers = []
	# Keep retry out of this case; we are testing ordering, not recovery.
	client.connect_retry = 60.0

	var connected := [false]
	client.native_connected.connect(
		func(id: int) -> void: if id == 1: connected[0] = true
	)
	client.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			host.deliver(_CLIENT_ID, "client", kind, payload)
	)

	# Hold the host's answer and candidates, then release the candidates first so
	# the client must queue them until set_remote_description lands.
	var held_cands: Array = []
	var held_answer := [null]
	var released := [false]
	host.signal_out.connect(
		func(_to: int, _sig: String, kind: String, payload: Dictionary) -> void:
			if released[0]:
				client.deliver(1, "host", kind, payload)
			elif kind == "answer":
				held_answer[0] = payload
			elif kind == "candidate":
				held_cands.append(payload)
			else:
				client.deliver(1, "host", kind, payload)
	)

	host.create_server()
	client.create_client(_CLIENT_ID)

	var release := func(frame: int) -> void:
		if frame == 40 and not released[0]:
			released[0] = true
			for c: Dictionary in held_cands:
				client.deliver(1, "host", "candidate", c)
			if held_answer[0] != null:
				client.deliver(1, "host", "answer", held_answer[0])
	await _pump_until(host, client, connected, release)

	assert_bool(connected[0]).is_true()
	host.close()
	client.close()


func test_dropped_first_offer_recovers_via_retry() -> void:
	var host := WebRTCSession.new()
	host.ice_servers = []
	var client := WebRTCSession.new()
	client.ice_servers = []
	# Retry fast so the dropped attempt re-offers quickly; the relay then widens
	# connect_retry the instant it forwards the recovered offer, so that attempt
	# completes as one clean re-offer instead of churning half-open links.
	client.connect_retry = 1.0
	client.max_connect_attempts = 3

	var connected := [false]
	client.native_connected.connect(
		func(id: int) -> void: if id == 1: connected[0] = true
	)

	# Swallow attempt one entirely (its offer, then its candidates) so the host
	# never starts a handshake and the client must re-offer to recover. Forward
	# from the second offer onward.
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

	assert_bool(dropped[0]).is_true()
	assert_bool(connected[0]).is_true()
	host.close()
	client.close()
