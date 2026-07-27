extends SceneTree
## Measures how much unreliable ENet traffic survives an irregularly polled peer.
##
## The owner lane rides [constant MultiplayerPeer.TRANSFER_MODE_UNRELIABLE],
## which Godot maps to ENet's unsequenced flag, and ENet throttles unreliable
## traffic at the sender from measured round-trip variance. Nothing in the addon
## calls [method ENetPacketPeer.throttle_configure], so the defaults decide it.
##
## A rendered two-process session recorded an authority polling every 20.19 ms
## with a spread of 11 to 41, against an owner sending every 16.67 ms. This runs
## real sockets at that cadence and counts what arrives, then repeats with the
## throttle pinned open so the difference is attributable.
##
## Run: godot --headless -s res://examples/racing/tools/enet_unreliable_loss_probe.gd

const PORT := 47311
const SECONDS := 12.0
const OWNER_PERIOD_MS := 1000.0 / 60.0
const PAYLOAD_BYTES := 220

# The authority poll cadence measured in the capture, cycled rather than drawn.
const JITTERY_POLL: Array[int] = [
	17, 20, 23, 19, 25, 16, 21, 18, 27, 20, 14, 22, 19, 24, 18, 21,
]

# The same authority polling at its declared rate.
const STEADY_POLL: Array[int] = [17, 17, 16, 17, 17, 16, 17, 17]


func _init() -> void:
	print("[enet] payload=%d bytes  owner period=%.2f ms  %.0f s per arm" % [
		PAYLOAD_BYTES,
		OWNER_PERIOD_MS,
		SECONDS,
	])
	_run_arm("steady-poll  default-throttle", STEADY_POLL, false)
	_run_arm("jittery-poll default-throttle", JITTERY_POLL, false)
	_run_arm("jittery-poll throttle-pinned", JITTERY_POLL, true)
	quit()


func _run_arm(label: String, poll_periods: Array[int], pin_throttle: bool) -> void:
	var server := ENetMultiplayerPeer.new()
	if server.create_server(PORT, 4) != OK:
		print("[enet] %s: could not bind port %d" % [label, PORT])
		return
	var client := ENetMultiplayerPeer.new()
	if client.create_client("127.0.0.1", PORT) != OK:
		print("[enet] %s: could not connect" % label)
		server.close()
		return

	# Settle the handshake before anything is counted.
	var guard := 0
	while client.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED \
			and guard < 2000:
		server.poll()
		client.poll()
		OS.delay_msec(2)
		guard += 1
	if client.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED:
		print("[enet] %s: never connected" % label)
		client.close()
		server.close()
		return

	if pin_throttle:
		# Deceleration zero keeps the throttle from ever backing off, so
		# unreliable traffic stops being rationed against round-trip variance.
		var peer := client.get_peer(1)
		if peer:
			peer.throttle_configure(1000, 2, 0)

	var payload := PackedByteArray()
	payload.resize(PAYLOAD_BYTES)
	for i in PAYLOAD_BYTES:
		payload[i] = i & 0xFF

	var sent := 0
	var received := 0
	var owner_at := 0.0
	var poll_at := 0.0
	var poll_index := 0
	var horizon := SECONDS * 1000.0
	var started := Time.get_ticks_msec()
	while owner_at < horizon:
		# Advance whichever peer is due next on the shared wall-time axis, then
		# sleep off the difference so the sockets see real timing.
		if owner_at <= poll_at:
			client.set_target_peer(1)
			client.set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
			if client.put_packet(payload) == OK:
				sent += 1
			client.poll()
			owner_at += OWNER_PERIOD_MS
		else:
			server.poll()
			while server.get_available_packet_count() > 0:
				server.get_packet()
				received += 1
			poll_at += float(poll_periods[poll_index % poll_periods.size()])
			poll_index += 1
		var target := int(minf(owner_at, poll_at))
		var elapsed := Time.get_ticks_msec() - started
		if target > elapsed:
			OS.delay_msec(target - elapsed)

	# Drain whatever is still in flight so the ratio is not short by the tail.
	for _i in 40:
		server.poll()
		client.poll()
		while server.get_available_packet_count() > 0:
			server.get_packet()
			received += 1
		OS.delay_msec(5)

	print("[enet] %-30s sent=%-5d received=%-5d ratio=%.3f" % [
		label,
		sent,
		received,
		float(received) / maxf(1.0, float(sent)),
	])
	client.close()
	server.close()
	for _i in 20:
		OS.delay_msec(2)
