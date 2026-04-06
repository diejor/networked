## Low-level in-process [MultiplayerPeerExtension] used by [LocalLoopbackSession].
##
## All packet routing is done via direct memory references — no real sockets are created.
## Peer IDs, connection status, and packet queues mirror the real [ENetMultiplayerPeer] API.
class_name LocalMultiplayerPeer
extends MultiplayerPeerExtension

## All currently linked remote peers keyed by their peer ID.
var linked_peers: Dictionary = {}

var _unique_id: int = 0
var _target_peer: int = 0
var _transfer_channel: int = 0
var _transfer_mode: TransferMode = TRANSFER_MODE_RELIABLE
var _is_server_peer: bool = false
var _connection_status: ConnectionStatus = CONNECTION_DISCONNECTED

var _closed: bool = false
var _closing: bool = false

var _peers_to_emit_connected: Array[int] = []
var _peers_to_emit_disconnected: Array[int] = []

var _packet_queue: Array[Dictionary] = []
var _current_packet: Dictionary = {}


## Initializes this peer as the server (unique ID [code]1[/code]).
func create_server() -> Error:
	_reset_state()
	_unique_id = 1
	_is_server_peer = true
	_connection_status = CONNECTION_CONNECTED
	NetLog.info("LocalMultiplayerPeer initialized as Server (ID: %d)" % _unique_id)
	return OK

## Initializes this peer as a client with [param client_id].
func create_client(client_id: int) -> Error:
	_reset_state()
	_unique_id = client_id
	_is_server_peer = false
	_connection_status = CONNECTION_CONNECTING
	NetLog.info("LocalMultiplayerPeer initialized as Client (ID: %d)" % _unique_id)
	return OK

## Links [param peer_reference] as a known remote peer with ID [param peer_id].
##
## On the server side, also queues a [code]peer_connected[/code] event.
func force_connect_peer(peer_id: int, peer_reference: LocalMultiplayerPeer) -> void:
	if _closed or _closing:
		return

	linked_peers[peer_id] = peer_reference
	NetLog.trace("Linked internally to peer %d." % peer_id)

	if _is_server_peer:
		_peers_to_emit_connected.append(peer_id)
		NetLog.trace("Queued peer_connected for Client %d." % peer_id)


func _poll() -> void:
	if (not _is_server_peer) and _connection_status == CONNECTION_CONNECTING:
		_connection_status = CONNECTION_CONNECTED
		_peers_to_emit_connected.append(1)
		NetLog.trace("Connection finalized. Added Server (1) to event queue.")

	while not _peers_to_emit_connected.is_empty():
		var p_id := _peers_to_emit_connected.pop_front()
		peer_connected.emit(p_id)
		NetLog.trace("Emitted peer_connected for %d." % p_id)

	while not _peers_to_emit_disconnected.is_empty():
		var p_id := _peers_to_emit_disconnected.pop_front()
		peer_disconnected.emit(p_id)
		NetLog.trace("Emitted peer_disconnected for %d." % p_id)

	if _closing:
		_finalize_close()

func _put_packet_script(p_buffer: PackedByteArray) -> Error:
	if _closed or _closing:
		return ERR_UNAVAILABLE

	NetLog.trace("Putting packet of %d bytes. Target: %d" % [p_buffer.size(), _target_peer])

	if _target_peer == 0:
		for peer_id in linked_peers.keys():
			_send_to_peer(peer_id, p_buffer)
		return OK

	if _target_peer < 0:
		for peer_id in linked_peers.keys():
			if peer_id != -_target_peer:
				_send_to_peer(peer_id, p_buffer)
		return OK

	# If we are a client and the target is not the server, we must route through the server for relaying.
	if not _is_server_peer and _target_peer != 1:
		NetLog.trace("Routing packet through server for relaying to %d." % _target_peer)
		return _send_to_peer(1, p_buffer)

	return _send_to_peer(_target_peer, p_buffer)

func _send_to_peer(peer_id: int, p_buffer: PackedByteArray) -> Error:
	if _closed or _closing:
		return ERR_UNAVAILABLE

	if not linked_peers.has(peer_id):
		NetLog.trace("Failed to send to %d: Not linked." % peer_id)
		return ERR_UNAVAILABLE

	var target: LocalMultiplayerPeer = linked_peers[peer_id]
	if target == null or target._closed or target._closing:
		linked_peers.erase(peer_id)
		NetLog.trace("Failed to send to %d: Target closed." % peer_id)
		return ERR_UNAVAILABLE

	NetLog.trace("Routing packet directly to %d's memory." % peer_id)
	target._receive_packet(p_buffer, _unique_id, _transfer_channel, _transfer_mode)
	return OK

func _receive_packet(p_buffer: PackedByteArray, p_sender: int, p_channel: int, p_mode: TransferMode) -> void:
	if _closed or _closing:
		return

	_packet_queue.append({
		"data": p_buffer,
		"peer": p_sender,
		"channel": p_channel,
		"mode": p_mode
	})
	NetLog.trace("Received packet from %d (Size: %d). Queue length: %d" %
		[p_sender, p_buffer.size(), _packet_queue.size()])

func _get_packet_script() -> PackedByteArray:
	if _packet_queue.is_empty():
		return PackedByteArray()

	_current_packet = _packet_queue.pop_front()
	var data: PackedByteArray = _current_packet.get("data", PackedByteArray())
	NetLog.trace("Popped packet from %d (Size: %d). Queue left: %d" %
		[_current_packet.get("peer", 0), data.size(), _packet_queue.size()])
	return data

func _purge_packets_from(sender_id: int) -> void:
	for i in range(_packet_queue.size() - 1, -1, -1):
		if _packet_queue[i].get("peer", 0) == sender_id:
			_packet_queue.remove_at(i)

	if _current_packet.get("peer", 0) == sender_id:
		_current_packet = {}


func _remote_closed(remote_id: int, remote_was_server: bool) -> void:
	_purge_packets_from(remote_id)
	linked_peers.erase(remote_id)

	_peers_to_emit_disconnected.append(remote_id)

	if remote_was_server and not _is_server_peer:
		_connection_status = CONNECTION_DISCONNECTED


func _close() -> void:
	if _closed or _closing:
		return

	NetLog.trace("Closing peer (scheduled).")
	_closing = true

	_connection_status = CONNECTION_DISCONNECTED

	var my_id := _unique_id
	var peers := linked_peers.keys() # snapshot

	if not _is_server_peer:
		_purge_packets_from(1)
		_peers_to_emit_disconnected.append(1)

	for peer_id in peers:
		var other: LocalMultiplayerPeer = linked_peers.get(peer_id)
		if other:
			other._remote_closed(my_id, _is_server_peer)

func _finalize_close() -> void:
	_closing = false
	_closed = true

	_peers_to_emit_connected.clear()
	_peers_to_emit_disconnected.clear()

	linked_peers.clear()
	_packet_queue.clear()
	_current_packet = {}

	_unique_id = 0
	_target_peer = 0
	_transfer_channel = 0
	_transfer_mode = TRANSFER_MODE_RELIABLE
	_is_server_peer = false

	NetLog.trace("Peer fully closed.")

func _disconnect_peer(p_peer: int, _p_force: bool) -> void:
	NetLog.trace("Disconnecting peer %d (scheduled)." % p_peer)

	linked_peers.erase(p_peer)
	_purge_packets_from(p_peer)
	_peers_to_emit_disconnected.append(p_peer)

	if not _is_server_peer and p_peer == 1:
		_connection_status = CONNECTION_DISCONNECTED


func _get_available_packet_count() -> int:
	return _packet_queue.size()

func _get_connection_status() -> ConnectionStatus:
	return _connection_status

func _get_max_packet_size() -> int:
	return 16777215

func _get_transfer_channel() -> int:
	return _transfer_channel

func _get_transfer_mode() -> TransferMode:
	return _transfer_mode

func _get_unique_id() -> int:
	return _unique_id

func _is_refusing_new_connections() -> bool:
	return false

func _is_server() -> bool:
	return _is_server_peer

func _is_server_relay_supported() -> bool:
	return true

func _set_refuse_new_connections(_p_enable: bool) -> void:
	pass

func _set_target_peer(p_peer: int) -> void:
	_target_peer = p_peer

func _set_transfer_channel(p_channel: int) -> void:
	_transfer_channel = p_channel

func _set_transfer_mode(p_mode: TransferMode) -> void:
	_transfer_mode = p_mode

func _get_packet_channel() -> int:
	if not _packet_queue.is_empty():
		return _packet_queue[0].get("channel", 0)
	return _current_packet.get("channel", 0)

func _get_packet_mode() -> TransferMode:
	if not _packet_queue.is_empty():
		return _packet_queue[0].get("mode", TRANSFER_MODE_RELIABLE)
	return _current_packet.get("mode", TRANSFER_MODE_RELIABLE)

func _get_packet_peer() -> int:
	if not _packet_queue.is_empty():
		return _packet_queue[0].get("peer", 0)
	return _current_packet.get("peer", 0)


func _reset_state() -> void:
	linked_peers.clear()
	_packet_queue.clear()
	_current_packet = {}
	_peers_to_emit_connected.clear()
	_peers_to_emit_disconnected.clear()

	_closing = false
	_closed = false

	_unique_id = 0
	_target_peer = 0
	_transfer_channel = 0
	_transfer_mode = TRANSFER_MODE_RELIABLE
	_is_server_peer = false
	_connection_status = CONNECTION_DISCONNECTED
