## Signaling-agnostic WebRTC peer machinery for a single room.
##
## A session owns the [WebRTCMultiplayerPeer], one [WebRTCPeerConnection] per
## remote, and the offer/answer/ICE exchange. It works entirely in engine
## [code]multiplayer_id[/code]s because [method WebRTCMultiplayerPeer.add_peer]
## and every RPC route on that id. The transport address is an opaque
## [code]signaler_id[/code] string the session stores and echoes but never
## parses, so no tracker detail leaks in.
## [codeblock]
## session.signal_out.connect(signaler.send)      # out: SDP/ICE to send
## signaler.received.connect(session.deliver)      # in: SDP/ICE received
##
## host:   session.create_server()
## client: session.create_client(multiplayer_id)
## each frame: session.poll()
## [/codeblock]
class_name WebRTCSession
extends RefCounted

## Emitted when the native WebRTC link to [param multiplayer_id] opens.
signal native_connected(multiplayer_id: int)
## Emitted when the native WebRTC link to [param multiplayer_id] drops.
signal native_disconnected(multiplayer_id: int)

## Emitted when SDP or ICE must reach a remote. [param kind] is
## [code]"offer"[/code], [code]"answer"[/code], or [code]"candidate"[/code].
## [param to_signaler_id] is empty until the session has learned the remote's
## address, which a discovery-capable signaler treats as room-directed.
signal signal_out(
	to_multiplayer_id: int,
	to_signaler_id: String,
	kind: String,
	payload: Dictionary,
)

## ICE server definitions passed to each [WebRTCPeerConnection].
var ice_servers: Array[Dictionary] = []

var webrtc_peer: WebRTCMultiplayerPeer = null

var _is_server := false
# multiplayer_id -> last known opaque signaler address, echoed on outbound.
var _signaler_ids: Dictionary = {}


## Creates the underlying peer in server mode. Mirrors
## [method WebRTCMultiplayerPeer.create_server].
func create_server() -> Error:
	_is_server = true
	var peer := WebRTCMultiplayerPeer.new()
	var err := peer.create_server()
	if err != OK:
		Netw.dbg.error("WebRTCSession create_server failed: %s", [error_string(err)])
		return err
	_bind_peer(peer)
	webrtc_peer = peer
	return OK


## Creates the underlying peer in client mode under [param multiplayer_id] and
## opens the initial connection to the server (multiplayer id 1).
func create_client(multiplayer_id: int) -> Error:
	_is_server = false
	var peer := WebRTCMultiplayerPeer.new()
	var err := peer.create_client(multiplayer_id)
	if err != OK:
		Netw.dbg.error("WebRTCSession create_client failed: %s", [error_string(err)])
		return err
	_bind_peer(peer)
	webrtc_peer = peer
	# The client opens the offer toward the server before any address is known.
	_ensure_connection(1, "")
	return OK


## Polls the underlying [WebRTCMultiplayerPeer].
func poll() -> void:
	if webrtc_peer:
		webrtc_peer.poll()


## Routes inbound SDP or ICE from [param from_signaler_id] into the connection
## for [param from_multiplayer_id], creating it on first contact.
func deliver(
	from_multiplayer_id: int,
	from_signaler_id: String,
	kind: String,
	payload: Dictionary,
) -> void:
	if webrtc_peer == null:
		return
	if not from_signaler_id.is_empty():
		_signaler_ids[from_multiplayer_id] = from_signaler_id
	_ensure_connection(from_multiplayer_id, from_signaler_id)
	match kind:
		"offer":
			_handle_offer(from_multiplayer_id, payload)
		"answer":
			_handle_answer(from_multiplayer_id, payload)
		"candidate":
			_handle_candidate(from_multiplayer_id, payload)


## Returns [code]true[/code] when a connection to [param multiplayer_id] exists.
func has_peer(multiplayer_id: int) -> bool:
	return webrtc_peer != null and webrtc_peer.has_peer(multiplayer_id)


## Closes the peer and clears per-remote address state.
func close() -> void:
	if webrtc_peer:
		webrtc_peer.close()
	webrtc_peer = null
	_signaler_ids.clear()


func _bind_peer(peer: WebRTCMultiplayerPeer) -> void:
	peer.peer_connected.connect(func(id: int) -> void: native_connected.emit(id))
	peer.peer_disconnected.connect(
		func(id: int) -> void: native_disconnected.emit(id)
	)


# Creates the WebRTCPeerConnection for multiplayer_id if absent. The client
# side calls create_offer toward the server (id 1).
func _ensure_connection(multiplayer_id: int, signaler_id: String) -> void:
	if webrtc_peer.has_peer(multiplayer_id):
		return
	if not signaler_id.is_empty():
		_signaler_ids[multiplayer_id] = signaler_id
	Netw.dbg.trace(
		"WebRTCSession: opening WebRTCPeerConnection for id %d", [multiplayer_id]
	)
	var connection := WebRTCPeerConnection.new()
	connection.initialize({ "iceServers": ice_servers })
	connection.session_description_created.connect(
		_on_session_description_created.bind(multiplayer_id)
	)
	connection.ice_candidate_created.connect(
		_on_ice_candidate_created.bind(multiplayer_id)
	)
	webrtc_peer.add_peer(connection, multiplayer_id)
	if not _is_server and multiplayer_id == 1:
		connection.create_offer()


func _handle_offer(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	var connection := _connection(multiplayer_id)
	var err := connection.set_remote_description("offer", payload.get("sdp", ""))
	if err != OK:
		Netw.dbg.debug(
			"WebRTCSession ignored stale offer for id %d: %s",
			[multiplayer_id, error_string(err)]
		)


func _handle_answer(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	_connection(multiplayer_id).set_remote_description(
		"answer", payload.get("sdp", "")
	)


func _handle_candidate(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	_connection(multiplayer_id).add_ice_candidate(
		payload.get("sdpMid", ""),
		payload.get("sdpMLineIndex", 0),
		payload.get("candidate", "")
	)


func _on_session_description_created(
	type: String, sdp: String, multiplayer_id: int
) -> void:
	var connection := _connection(multiplayer_id)
	connection.set_local_description(type, sdp)
	signal_out.emit(
		multiplayer_id,
		String(_signaler_ids.get(multiplayer_id, "")),
		type,
		{ "type": type, "sdp": sdp },
	)


func _on_ice_candidate_created(
	media: String, index: int, name: String, multiplayer_id: int
) -> void:
	signal_out.emit(
		multiplayer_id,
		String(_signaler_ids.get(multiplayer_id, "")),
		"candidate",
		{
			"type": "candidate",
			"candidate": name,
			"sdpMid": media,
			"sdpMLineIndex": index,
		},
	)


func _connection(multiplayer_id: int) -> WebRTCPeerConnection:
	return webrtc_peer.get_peer(multiplayer_id).get("connection")
