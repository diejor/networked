## Shared in-process session linking [LocalMultiplayerPeer] instances.
##
## One process-wide singleton is maintained via [method get_shared_session].
class_name LocalLoopbackSession
extends Resource

static var shared: LocalLoopbackSession = null

var server_peer: LocalMultiplayerPeer
var client_peers: Array[LocalMultiplayerPeer] = []
var server_app_id: StringName = &""
var _poll_count: int = 0
var _links_by_peer: Dictionary = { }
var _held_peers: Dictionary = { }


class _LinkState:
	var conditions_by_sender: Dictionary[int, NetwLinkConditions] = { }
	var in_flight: Array[Dictionary] = []
	var seq: int = 0


## Returns the process-wide shared session, creating it on first access.
static func get_shared_session() -> LocalLoopbackSession:
	if not shared:
		shared = LocalLoopbackSession.new()
	return shared


## Returns [code]true[/code] if the server peer exists and is connected.
func has_live_server() -> bool:
	return (
			server_peer != null
			and server_peer.get_connection_status()
			!= MultiplayerPeer.CONNECTION_DISCONNECTED
	)


func init_server_side() -> void:
	if has_live_server():
		return

	if server_peer:
		server_peer.close()

	server_peer = LocalMultiplayerPeer.new()
	var err := server_peer.create_server()
	if err != OK:
		Netw.dbg.warn(
			"Loopback: server create_server failed",
			func(m): push_warning(m)
		)


## Creates and links a new client peer to the server.
##
## Returns the new [LocalMultiplayerPeer].
func create_client_peer() -> LocalMultiplayerPeer:
	init_server_side()

	var client := LocalMultiplayerPeer.new()
	var client_id := randi_range(2, 2147483647)
	var err := client.create_client(client_id)
	if err != OK:
		Netw.dbg.warn(
			"Loopback: client create_client failed",
			func(m): push_warning(m)
		)
		return client

	server_peer.force_connect_peer(client_id, client)
	client.force_connect_peer(1, server_peer)
	client_peers.append(client)
	Netw.dbg.info("Local loopback handshake complete for client %d." % client_id)
	return client


## Returns the server peer, initializing it first if necessary.
func get_server_peer() -> LocalMultiplayerPeer:
	init_server_side()
	return server_peer


## Convenience wrapper around [method create_client_peer].
func get_client_peer() -> LocalMultiplayerPeer:
	return create_client_peer()


## Polls the server and all active client peers each frame.
func poll() -> void:
	_poll_count += 1
	if server_peer:
		_poll_or_hold(server_peer)
	for client in client_peers:
		if client and not client._closed:
			_poll_or_hold(client)


## Holds inbound packets for [param peer] until
## [method release_inbound_packets] is called.
##
## Existing queued packets are held immediately. New packets are captured
## during [method poll], preserving delivery order for release.
func hold_inbound_packets(peer: LocalMultiplayerPeer) -> void:
	if not peer:
		return
	_held_peers[peer] = true
	_capture_held_packets(peer)


## Releases packets previously captured by
## [method hold_inbound_packets], delivering them before newer queued
## packets on the same [param peer].
func release_inbound_packets(peer: LocalMultiplayerPeer) -> void:
	if not peer or not _held_peers.has(peer):
		return
	_capture_held_packets(peer)
	_held_peers.erase(peer)
	_release_due(peer, true)


## Sets deterministic inbound link conditions for [param peer].
func set_link_conditions(
		peer: LocalMultiplayerPeer,
		conditions: NetwLinkConditions,
		sender_id: int = 0,
) -> void:
	if not peer:
		return
	var state := _ensure_link(peer)
	if not conditions:
		state.conditions_by_sender.erase(sender_id)
		return
	var copy := conditions.clone() as NetwLinkConditions
	copy.reset_rng()
	state.conditions_by_sender[sender_id] = copy


## Clears inbound link conditions for [param peer] and [param sender_id].
func clear_link_conditions(
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> void:
	if not peer or not _links_by_peer.has(peer):
		return
	var state: _LinkState = _links_by_peer[peer]
	state.conditions_by_sender.erase(sender_id)
	if not _has_link_conditions(state) and not _held_peers.has(peer):
		_release_due(peer, true)
		if state.in_flight.is_empty():
			_links_by_peer.erase(peer)


## Returns the installed inbound link conditions for [param peer].
func get_link_conditions(
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> NetwLinkConditions:
	if not peer or not _links_by_peer.has(peer):
		return null
	var state: _LinkState = _links_by_peer[peer]
	return state.conditions_by_sender.get(sender_id) as NetwLinkConditions


## Closes all peers and resets the session so a new server can be hosted.
func reset() -> void:
	if server_peer:
		server_peer.close()
	for client in client_peers:
		if client:
			client.close()
	server_peer = null
	server_app_id = &""
	client_peers.clear()
	_poll_count = 0
	_links_by_peer.clear()
	_held_peers.clear()


func _poll_or_hold(peer: LocalMultiplayerPeer) -> void:
	if _held_peers.has(peer):
		_capture_held_packets(peer)
		return

	var state := _links_by_peer.get(peer) as _LinkState
	if state and _has_link_conditions(state):
		peer.poll()
		_capture_conditioned_packets(peer, state)
		_release_due(peer)
		return

	if state and not state.in_flight.is_empty():
		_release_due(peer)
		if state.in_flight.is_empty():
			_links_by_peer.erase(peer)
		return

	peer.poll()


func _capture_held_packets(peer: LocalMultiplayerPeer) -> void:
	if peer._packet_queue.is_empty():
		return
	var state := _ensure_link(peer)
	for packet in peer._packet_queue:
		_enqueue_packet(state, packet, 9223372036854775807)
	peer._packet_queue.clear()


func _capture_conditioned_packets(
		peer: LocalMultiplayerPeer,
		state: _LinkState,
) -> void:
	if peer._packet_queue.is_empty():
		return
	var passthrough: Array[Dictionary] = []
	for packet in peer._packet_queue:
		var conditions := _conditions_for_packet(state, packet)
		if not conditions:
			passthrough.append(packet)
			continue
		if _should_drop_packet(packet, conditions):
			continue
		var due_poll := _poll_count + maxi(0, conditions.delay_polls)
		if _can_reorder(packet):
			due_poll += conditions._draw_jitter()
			if conditions._roll(conditions.reorder_probability):
				due_poll += 1
		_enqueue_packet(state, packet, due_poll)
	peer._packet_queue = passthrough


func _release_due(peer: LocalMultiplayerPeer, flush_all: bool = false) -> void:
	if not _links_by_peer.has(peer) or peer._closed or peer._closing:
		return

	var state: _LinkState = _links_by_peer[peer]
	state.in_flight.sort_custom(_compare_in_flight)

	var due: Array[Dictionary] = []
	var remaining: Array[Dictionary] = []
	for entry: Dictionary in state.in_flight:
		if flush_all or entry.get("due_poll", 0) <= _poll_count:
			due.append(entry)
		else:
			remaining.append(entry)
	state.in_flight = remaining

	if due.is_empty():
		return

	var existing := peer._packet_queue.duplicate()
	peer._packet_queue.clear()
	for entry: Dictionary in due:
		var packet: Dictionary = entry.get("packet", { })
		if _sender_is_linked(peer, packet):
			peer._packet_queue.append(packet)
	peer._packet_queue.append_array(existing)
	if state.in_flight.is_empty() \
			and not _has_link_conditions(state) \
			and not _held_peers.has(peer):
		_links_by_peer.erase(peer)


func _has_link_conditions(state: _LinkState) -> bool:
	return state.conditions_by_sender.size() > 0


func _conditions_for_packet(
		state: _LinkState,
		packet: Dictionary,
) -> NetwLinkConditions:
	var sender_id: int = packet.get("peer", 0)
	if state.conditions_by_sender.has(sender_id):
		return state.conditions_by_sender[sender_id]
	return state.conditions_by_sender.get(0) as NetwLinkConditions


func _ensure_link(peer: LocalMultiplayerPeer) -> _LinkState:
	if not _links_by_peer.has(peer):
		_links_by_peer[peer] = _LinkState.new()
	return _links_by_peer[peer]


func _enqueue_packet(
		state: _LinkState,
		packet: Dictionary,
		due_poll: int,
) -> void:
	state.in_flight.append(
		{
			"packet": packet,
			"due_poll": due_poll,
			"seq": state.seq,
		},
	)
	state.seq += 1


func _should_drop_packet(
		packet: Dictionary,
		conditions: NetwLinkConditions,
) -> bool:
	if not conditions:
		return false
	if not conditions.drop_reliable and not _can_reorder(packet):
		return false
	return conditions._roll(conditions.loss_probability)


func _can_reorder(packet: Dictionary) -> bool:
	return packet.get("mode", MultiplayerPeer.TRANSFER_MODE_RELIABLE) \
			!= MultiplayerPeer.TRANSFER_MODE_RELIABLE


func _sender_is_linked(
		peer: LocalMultiplayerPeer,
		packet: Dictionary,
) -> bool:
	var sender_id: int = packet.get("peer", 0)
	return sender_id == 0 or peer.linked_peers.has(sender_id)


func _compare_in_flight(a: Dictionary, b: Dictionary) -> bool:
	var a_due: int = a.get("due_poll", 0)
	var b_due: int = b.get("due_poll", 0)
	if a_due == b_due:
		return int(a.get("seq", 0)) < int(b.get("seq", 0))
	return a_due < b_due
