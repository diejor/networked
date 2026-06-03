## Shared in-process session linking [LocalMultiplayerPeer] instances.
##
## One process-wide singleton is maintained via [method get_shared_session].
class_name LocalLoopbackSession
extends Resource

static var shared: LocalLoopbackSession = null

var server_peer: LocalMultiplayerPeer
var client_peers: Array[LocalMultiplayerPeer] = []
var server_app_id: StringName = &""
var _held_packets_by_peer: Dictionary = { }


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
	if not _held_packets_by_peer.has(peer):
		_held_packets_by_peer[peer] = []
	_capture_held_packets(peer)


## Releases packets previously captured by
## [method hold_inbound_packets], delivering them before newer queued
## packets on the same [param peer].
func release_inbound_packets(peer: LocalMultiplayerPeer) -> void:
	if not peer or not _held_packets_by_peer.has(peer):
		return
	_capture_held_packets(peer)
	var held: Array = _held_packets_by_peer[peer]
	_held_packets_by_peer.erase(peer)
	var existing := peer._packet_queue.duplicate()
	peer._packet_queue.clear()
	peer._packet_queue.append_array(held)
	peer._packet_queue.append_array(existing)


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
	_held_packets_by_peer.clear()


func _poll_or_hold(peer: LocalMultiplayerPeer) -> void:
	if _held_packets_by_peer.has(peer):
		_capture_held_packets(peer)
		return
	peer.poll()


func _capture_held_packets(peer: LocalMultiplayerPeer) -> void:
	var held: Array = _held_packets_by_peer.get(peer, [])
	held.append_array(peer._packet_queue)
	peer._packet_queue.clear()
	_held_packets_by_peer[peer] = held
