extends MultiplayerPeerExtension

## A [MultiplayerPeer] that sends packets through a Nakama match.
##
## [NakamaRelayBridge] creates it, adds peers and delivers received packets.
class_name NakamaRelayPeer

const MAX_PACKET_SIZE := 1 << 24

var _self_id := 0
var _connection_status: ConnectionStatus = CONNECTION_DISCONNECTED
var _refusing_new_connections := false
var _target_id := 0
var _connected_peers: Array[int] = []
var _peers_to_emit_connected: Array[int] = []
var _peers_to_emit_disconnected: Array[int] = []
var _current_packet: Packet = null
var _term := 0
var _relabels: Array[Callable] = []
var _transfer_handler := Callable()


class Packet extends RefCounted:
	var data: PackedByteArray
	var from: int
	var term: int


	func _init(p_data: PackedByteArray, p_from: int, p_term: int) -> void:
		data = p_data
		from = p_from
		term = p_term


var _incoming_packets := []

## Emitted when Godot writes a packet for the bridge to send to Nakama.
signal packet_generated(peer_id, buffer)

## Emitted from [method MultiplayerPeer.poll] once the peer that was
## [param successor_was] answers as [code]1[/code], and the old server, if it
## stayed, as [param former_now].
signal server_relabelled(successor_was: int, former_now: int)


func _get_packet_script() -> PackedByteArray:
	_drop_stale_packets()
	if _incoming_packets.size() == 0:
		return PackedByteArray()
	_current_packet = _incoming_packets.pop_front()
	return _current_packet.data


func _get_packet_mode() -> TransferMode:
	return TRANSFER_MODE_RELIABLE


func _get_packet_channel() -> int:
	return 0


func _put_packet_script(p_buffer: PackedByteArray) -> Error:
	packet_generated.emit(_target_id, p_buffer)
	return OK


func _get_available_packet_count() -> int:
	_drop_stale_packets()
	return _incoming_packets.size()


func _get_max_packet_size() -> int:
	return MAX_PACKET_SIZE


func _set_transfer_channel(p_channel) -> void:
	pass


func _get_transfer_channel() -> int:
	return 0


func _set_transfer_mode(p_mode: TransferMode) -> void:
	pass


func _get_transfer_mode() -> TransferMode:
	return TRANSFER_MODE_RELIABLE


func _set_target_peer(p_peer_id: int) -> void:
	_target_id = p_peer_id


func _get_packet_peer() -> int:
	_drop_stale_packets()
	if not _incoming_packets.is_empty():
		return _incoming_packets[0].from
	if _current_packet != null:
		return _current_packet.from
	return 0


func _is_server() -> bool:
	return _self_id == 1


func _is_server_relay_supported() -> bool:
	return false


func _poll() -> void:
	while not _relabels.is_empty():
		var install: Callable = _relabels.pop_front()
		var labels: Array = install.call()
		if labels.size() == 2:
			server_relabelled.emit(labels[0], labels[1])
	while not _peers_to_emit_connected.is_empty():
		peer_connected.emit(_peers_to_emit_connected.pop_front())
	while not _peers_to_emit_disconnected.is_empty():
		peer_disconnected.emit(_peers_to_emit_disconnected.pop_front())


func _get_peers() -> PackedInt32Array:
	return PackedInt32Array(_connected_peers)


func _get_unique_id() -> int:
	return _self_id


func _set_refuse_new_connections(p_enable: bool) -> void:
	_refusing_new_connections = p_enable


func _is_refusing_new_connections() -> bool:
	return _refusing_new_connections


func _get_connection_status() -> ConnectionStatus:
	return _connection_status


## Marks the local peer id assigned by the bridge.
func initialize(p_self_id: int) -> void:
	if _connection_status != CONNECTION_CONNECTING:
		return
	_self_id = p_self_id
	if _self_id == 1:
		_connection_status = CONNECTION_CONNECTED


## Sets the connection status reported through [MultiplayerPeer].
func set_connection_status(p_connection_status: int) -> void:
	_connection_status = p_connection_status


## Registers [param p_peer_id] and schedules [signal peer_connected].
func register_peer(p_peer_id: int) -> void:
	if p_peer_id == _self_id or _connected_peers.has(p_peer_id):
		return
	_connected_peers.append(p_peer_id)
	_peers_to_emit_connected.append(p_peer_id)


## Unregisters [param p_peer_id] and schedules [signal peer_disconnected].
func unregister_peer(p_peer_id: int) -> void:
	if not _connected_peers.has(p_peer_id):
		return
	_connected_peers.erase(p_peer_id)
	_peers_to_emit_disconnected.append(p_peer_id)
	_incoming_packets = _incoming_packets.filter(
		func(packet: Packet) -> bool: return packet.from != p_peer_id
	)
	if p_peer_id == 1 and _self_id != 1:
		_connection_status = CONNECTION_DISCONNECTED


## Queues a packet its sender wrote under transfer [param p_term] for Godot to
## poll.
func deliver_packet(
		p_data: PackedByteArray,
		p_from_peer_id: int,
		p_term: int = 0,
) -> void:
	var packet = Packet.new(p_data, p_from_peer_id, p_term)
	_incoming_packets.push_back(packet)


## Hands the server role to [param peer_id] and makes this peer a client.
## [br][br]
## [b]Server Only.[/b]
func transfer_server(peer_id: int) -> Error:
	if not _transfer_handler.is_valid():
		return ERR_UNAVAILABLE
	return _transfer_handler.call(peer_id)


## Runs [param install] at the start of the next poll, then emits
## [signal server_relabelled] with the two ids it answers.
func schedule_relabel(install: Callable) -> void:
	_relabels.append(install)


## Adopts [param p_self_id] and [param p_peers] as the ids of transfer
## [param p_term].
func relabel(p_self_id: int, p_peers: Array[int], p_term: int) -> void:
	_self_id = p_self_id
	_term = p_term
	_connected_peers = p_peers.duplicate()
	_peers_to_emit_connected = _peers_to_emit_connected.filter(
		func(id: int) -> bool: return p_peers.has(id)
	)
	_current_packet = null


func _drop_stale_packets() -> void:
	while not _incoming_packets.is_empty() and _incoming_packets[0].term < _term:
		_incoming_packets.pop_front()


func _close() -> void:
	_connection_status = CONNECTION_DISCONNECTED
	_self_id = 0
	_term = 0
	_relabels.clear()
	_target_id = 0
	_connected_peers.clear()
	_peers_to_emit_connected.clear()
	_peers_to_emit_disconnected.clear()
	_current_packet = null
	_incoming_packets.clear()


func _disconnect_peer(p_peer: int, _p_force: bool) -> void:
	unregister_peer(p_peer)
