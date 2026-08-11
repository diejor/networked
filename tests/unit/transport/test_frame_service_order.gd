## Unit tests for the intra-frame service order: everything that services a
## carrier runs before the transport read, so a packet a carrier queues is read
## in the frame it arrived rather than the next one.
class_name TestFrameServiceOrder
extends NetwTestSuite

# Records the transport read. MultiplayerAPI.poll() polls its assigned peer, so
# this peer's _poll is the read every carrier pump has to precede. It carries no
# traffic: the order is the whole subject, so a peer that only reports itself
# connected is all the law needs.
class _RecordingPeer:
	extends MultiplayerPeerExtension

	var seq: Array[String] = []


	func _poll() -> void:
		seq.append("transport")


	func _get_connection_status() -> ConnectionStatus:
		return CONNECTION_CONNECTED


	func _get_unique_id() -> int:
		return 1


	func _is_server() -> bool:
		return true


	func _get_available_packet_count() -> int:
		return 0


# Records the carrier pump, standing in for the loopback bus and the WebRTC
# signaling that really run there.
class _RecordingView:
	extends NetwPeerView

	var seq: Array[String] = []


	func poll(_dt: float) -> void:
		seq.append("view")


func after_test() -> void:
	LocalLoopbackSession.get_shared_session().reset()


func _server_api() -> NetwMultiplayer:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [LocalTransport.new()]
	return api


# SceneMultiplayer drops every inbound packet until it is rooted, so a delivery
# assertion on an unrooted rig would measure the root check, not the order.
func _rooted_api() -> NetwMultiplayer:
	var api := _server_api()
	api.inner.root_path = get_tree().root.get_path()
	return api


func test_poll_started_precedes_the_transport_read() -> void:
	var api := _server_api()
	var peer := _RecordingPeer.new()
	api.multiplayer_peer = peer

	api.poll_started.connect(func(_dt: float) -> void: peer.seq.append("started"))
	api.poll()

	assert_array(peer.seq).contains_exactly(["started", "transport"])
	api.embedding.dispose()


func test_the_peer_view_is_pumped_before_the_transport_read() -> void:
	# The regression this guards: a view pumped after the read delivers every
	# queued packet a frame late, with nothing logged anywhere.
	var api := _server_api()
	var peer := _RecordingPeer.new()
	var view := _RecordingView.new()
	view.seq = peer.seq
	api.multiplayer_peer = peer
	NetwConnector.of(api)._set_peer_view(view)

	api.poll()

	assert_array(peer.seq).contains_exactly(["view", "transport"])
	api.embedding.dispose()


func test_a_delayed_packet_arrives_in_the_poll_that_pumped_the_carrier() -> void:
	# The end-to-end shape of the same law: the bus releases a due packet during
	# the pump, so the read that follows it in the same poll sees the packet.
	# Pump after read and every delivery slips a frame, silently.
	var bus := LocalLoopbackSession.new()
	var host := _rooted_api()
	var client := _rooted_api()
	host.multiplayer_peer = bus.get_server_peer()
	var client_peer := bus.create_client_peer()
	client.multiplayer_peer = client_peer
	# The carrier is serviced from the tick, which is where a transport's view
	# pump rides in production.
	host.poll_started.connect(func(_dt: float) -> void: bus.poll())
	client.poll_started.connect(func(_dt: float) -> void: bus.poll())

	var client_id := client_peer.get_unique_id()
	var guard := 0
	while not host.get_peers().has(client_id) and guard < 60:
		host.poll()
		client.poll()
		await get_tree().process_frame
		guard += 1
	assert_array(host.get_peers()).contains([client_id])

	# A link with latency holds the packet until a pump releases it, which is
	# what makes the order observable at all.
	var conditions := LocalLinkConditions.create()
	conditions.latency_ms = 5.0
	bus.set_link_conditions(client_peer, conditions)

	var received: Array[PackedByteArray] = []
	client.peer_packet.connect(
		func(_id: int, packet: PackedByteArray) -> void:
			received.append(packet)
	)

	host.send_bytes("ping".to_utf8_buffer(), client_id)
	client.poll()

	assert_int(received.size()).is_equal(1)
	bus.reset()
	host.embedding.dispose()
	client.embedding.dispose()
