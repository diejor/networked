class_name LocalMultiplayerPeer
extends MultiplayerPeerExtension

@export var debug_mode: bool = false

var linked_peers: Dictionary = {}

var _unique_id: int = 0
var _target_peer: int = 0
var _transfer_channel: int = 0
var _transfer_mode: TransferMode = TRANSFER_MODE_RELIABLE
var _is_server_peer: bool = false
var _connection_status: ConnectionStatus = CONNECTION_DISCONNECTED

var _packet_queue: Array[Dictionary] = []
var _current_packet: Dictionary = {}

var _peers_to_emit_connected: Array[int] = []
var _peers_to_emit_disconnected: Array[int] = []

func _log(message: String) -> void:
	if not debug_mode: return
	var role: String = "Server" if _is_server_peer else "Client %d" % _unique_id
	print("[%s] %s" % [role, message])

func create_server() -> Error:
	_unique_id = 1
	_is_server_peer = true
	_connection_status = CONNECTION_CONNECTED
	_log("Initialized as Server.")
	return OK

func create_client(client_id: int) -> Error:
	_unique_id = client_id
	_is_server_peer = false
	_connection_status = CONNECTION_CONNECTING
	_log("Initialized as Client.")
	return OK

func _poll() -> void:
	if not _is_server_peer and _connection_status == CONNECTION_CONNECTING:
		_connection_status = CONNECTION_CONNECTED
		_peers_to_emit_connected.append(1)
		_log("Connection finalized. Added Server (1) to event queue.")

	while not _peers_to_emit_connected.is_empty():
		var p_id: int = _peers_to_emit_connected.pop_front()
		peer_connected.emit(p_id)
		_log("Emitted peer_connected for %d." % p_id)
		
	while not _peers_to_emit_disconnected.is_empty():
		var p_id: int = _peers_to_emit_disconnected.pop_front()
		peer_disconnected.emit(p_id)
		_log("Emitted peer_disconnected for %d." % p_id)

func force_connect_peer(peer_id: int, peer_reference: LocalMultiplayerPeer) -> void:
	linked_peers[peer_id] = peer_reference
	_log("Linked internally to peer %d." % peer_id)
	if _is_server_peer:
		_peers_to_emit_connected.append(peer_id)
		_log("Queued peer_connected for Client %d." % peer_id)

func _receive_packet(p_buffer: PackedByteArray, p_sender: int, p_channel: int, p_mode: TransferMode) -> void:
	_packet_queue.append({
		"data": p_buffer,
		"peer": p_sender,
		"channel": p_channel,
		"mode": p_mode
	})
	_log("Received packet from %d (Size: %d). Queue length: %d" % [p_sender, p_buffer.size(), _packet_queue.size()])

func _put_packet_script(p_buffer: PackedByteArray) -> Error:
	_log("Putting packet of %d bytes. Target: %d" % [p_buffer.size(), _target_peer])
	
	if _target_peer == 0:
		for peer_id in linked_peers:
			_send_to_peer(peer_id, p_buffer)
		return OK
	
	if _target_peer < 0:
		for peer_id in linked_peers:
			if peer_id != -_target_peer:
				_send_to_peer(peer_id, p_buffer)
		return OK
		
	return _send_to_peer(_target_peer, p_buffer)

func _send_to_peer(peer_id: int, p_buffer: PackedByteArray) -> Error:
	if not linked_peers.has(peer_id):
		_log("Failed to send to %d: Not linked." % peer_id)
		return ERR_UNAVAILABLE
		
	_log("Routing packet directly to %d's memory." % peer_id)
	var target: LocalMultiplayerPeer = linked_peers[peer_id]
	target._receive_packet(p_buffer, _unique_id, _transfer_channel, _transfer_mode)
	return OK

func _get_packet_script() -> PackedByteArray:
	if _packet_queue.is_empty(): return PackedByteArray()
	_current_packet = _packet_queue.pop_front()
	var data: PackedByteArray = _current_packet.get("data", PackedByteArray())
	_log("Popped packet from %d (Size: %d). Queue left: %d" % [_current_packet.get("peer", 0), data.size(), _packet_queue.size()])
	return data

func _get_packet_channel() -> int:
	if not _packet_queue.is_empty(): return _packet_queue[0].get("channel", 0)
	return _current_packet.get("channel", 0)

func _get_packet_mode() -> TransferMode:
	if not _packet_queue.is_empty(): return _packet_queue[0].get("mode", TRANSFER_MODE_RELIABLE)
	return _current_packet.get("mode", TRANSFER_MODE_RELIABLE)

func _get_packet_peer() -> int:
	if not _packet_queue.is_empty(): return _packet_queue[0].get("peer", 0)
	return _current_packet.get("peer", 0)

func _close() -> void:
	_log("Closing peer.")
	_connection_status = CONNECTION_DISCONNECTED
	linked_peers.clear()
	_packet_queue.clear()

func _disconnect_peer(p_peer: int, p_force: bool) -> void:
	_log("Disconnecting peer %d." % p_peer)
	linked_peers.erase(p_peer)
	_peers_to_emit_disconnected.append(p_peer)

func _get_available_packet_count() -> int: return _packet_queue.size()
func _get_connection_status() -> ConnectionStatus: return _connection_status
func _get_max_packet_size() -> int: return 16777215
func _get_transfer_channel() -> int: return _transfer_channel
func _get_transfer_mode() -> TransferMode: return _transfer_mode
func _get_unique_id() -> int: return _unique_id
func _is_refusing_new_connections() -> bool: return false
func _is_server() -> bool: return _is_server_peer
func _is_server_relay_supported() -> bool: return _is_server_peer
func _set_refuse_new_connections(_p_enable: bool) -> void: pass
func _set_target_peer(p_peer: int) -> void: _target_peer = p_peer
func _set_transfer_channel(p_channel: int) -> void: _transfer_channel = p_channel
func _set_transfer_mode(p_mode: TransferMode) -> void: _transfer_mode = p_mode
