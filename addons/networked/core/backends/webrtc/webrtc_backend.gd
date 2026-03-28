@tool
class_name WebRTCBackend
extends BackendPeer

signal signaling_connected
signal signaling_disconnected
signal room_created(room_id: String)


@export var trackers: Array[String] = [
	"wss://tracker.openwebtorrent.com",
	"wss://tracker.files.fm:7073/announce",
	"wss://tracker.webtorrent.dev"
]

@export var ice_servers: Array[Dictionary] = [
	{ "urls": ["stun:stun.l.google.com:19302"] },
	{ "urls": ["turn:openrelay.metered.ca:80"], "username": "openrelayproject", "credential": "openrelayproject" }
]
@export_group("Debug")
@export var debug_mode: bool = false
@export_group("Debug")

var webrtc_peer: WebRTCMultiplayerPeer:
	get: return api.multiplayer_peer as WebRTCMultiplayerPeer
	set(peer): api.multiplayer_peer = peer

var _sockets: Array[WebSocketPeer] = []
var _is_server := false
var _info_hash := ""
var _local_peer_id := ""
var _server_wt_id := ""
var _client_offer_sdp := ""
var _client_offer_id := "" 
var _client_candidate_queue: Array[Dictionary] = []
var _peer_map := {} 
var _local_godot_id := 0
var _announce_timer := 0.0

func _log(msg: String) -> void:
	if debug_mode:
		var prefix = "[HOST]" if _is_server else "[CLIENT]"
		print("%s %s" % [prefix, msg])

func host() -> Error:
	_is_server = true
	_local_godot_id = 1
	_local_peer_id = _generate_peer_id(_local_godot_id)
	_info_hash = _generate_hash() 
	_reset_state_vars()
	
	_log("Starting Host. Local WebTorrent ID: " + _local_peer_id.substr(0, 6) + "...")
	
	var peer := WebRTCMultiplayerPeer.new()
	var err := peer.create_server()
	if err != OK:
		push_error("Failed to create WebRTC server: ", error_string(err))
		return err
		
	_bind_webrtc_signals(peer)
	webrtc_peer = peer
	room_created.emit(_info_hash)
	
	NetLog.info("Room session ready at `%s` (saved to clipboard)." % _info_hash)
	DisplayServer.clipboard_set(_info_hash)
	
	return _connect_trackers()

func join(server_address: String, _username: String = "") -> Error:
	_is_server = false
	_local_godot_id = randi() % 1000000 + 2
	_local_peer_id = _generate_peer_id(_local_godot_id)
	
	if server_address.length() != 20:
		_info_hash = server_address.sha1_text().substr(0, 20)
	else:
		_info_hash = server_address
		
	_reset_state_vars()
	
	_log("Starting Client. Local Godot ID: " + str(_local_godot_id))
	_log("Joining Room Hash: " + _info_hash)
	
	var peer := WebRTCMultiplayerPeer.new()
	var err := peer.create_client(_local_godot_id)
	if err != OK:
		push_error("Failed to create WebRTC client: ", error_string(err))
		return err
		
	_bind_webrtc_signals(peer)
	webrtc_peer = peer
	_log("Client Peer Created. Generating initial WebRTC Connection to Server...")
	_create_peer_connection(1, "") 
	
	return _connect_trackers()

func poll(dt: float) -> void:
	super.poll(dt)
	if webrtc_peer:
		webrtc_peer.poll()
		
	if not _sockets.is_empty():
		_poll_trackers(dt)

func _bind_webrtc_signals(peer: WebRTCMultiplayerPeer) -> void:
	if not peer.peer_connected.is_connected(_on_webrtc_peer_connected):
		peer.peer_connected.connect(_on_webrtc_peer_connected)
		peer.peer_disconnected.connect(_on_webrtc_peer_disconnected)

func _on_webrtc_peer_connected(id: int) -> void:
	_log(">>> SUCCESS! WebRTC Native Connection Established with Godot ID: " + str(id) + " <<<")
	if not _is_server and id == 1:
		_log("WebRTC active. Closing signaling trackers.")
		for ws in _sockets:
			ws.close()
		_sockets.clear()
		signaling_disconnected.emit()

func _on_webrtc_peer_disconnected(id: int) -> void:
	_log(">>> WebRTC Native Connection Lost with Godot ID: " + str(id) + " <<<")

func get_join_address() -> String:
	if not _info_hash.is_empty():
		return _info_hash
	return super.get_join_address()

func peer_reset_state() -> void:
	super.peer_reset_state()
	_log("Resetting Peer State.")
	for ws in _sockets:
		ws.close()
	_sockets.clear()
	_is_server = false
	_info_hash = ""
	_local_peer_id = ""
	_reset_state_vars()
	_local_godot_id = 0

func _reset_state_vars() -> void:
	_server_wt_id = ""
	_client_offer_sdp = ""
	_client_offer_id = ""
	_announce_timer = 0.0
	_client_candidate_queue.clear()
	_peer_map.clear()

func _generate_hash() -> String:
	var chars := "0123456789abcdef"
	var hash_str := ""
	for i in 20: 
		hash_str += chars[randi() % chars.length()]
	return hash_str

func _generate_peer_id(godot_id: int) -> String:
	var chars := "0123456789abcdef"
	var prefix := ""
	for i in 10: 
		prefix += chars[randi() % chars.length()]
	return prefix + str(godot_id).pad_zeros(10)

func _connect_trackers() -> Error:
	_sockets.clear()
	var connected_count := 0
	
	for url in trackers:
		_log("Connecting to Tracker: " + url)
		var ws := WebSocketPeer.new()
		if ws.connect_to_url(url) == OK:
			_sockets.append(ws)
			ws.set_meta("url", url)
			connected_count += 1
		else:
			_log("Failed to connect to Tracker: " + url)
			
	if connected_count == 0:
		return ERR_CANT_CONNECT
	
	return OK

func _poll_trackers(dt: float) -> void:
	var any_open := false
	_announce_timer += dt
	
	var should_reannounce = false
	if not _is_server and _server_wt_id.is_empty() and _announce_timer > 2.0:
		should_reannounce = true
		_announce_timer = 0.0
	
	for ws in _sockets:
		ws.poll()
		var state := ws.get_ready_state()
		
		if state == WebSocketPeer.STATE_OPEN:
			any_open = true
			if not ws.has_meta("announced") or should_reannounce:
				if not ws.has_meta("announced"):
					_log("Tracker Connected: " + ws.get_meta("url", "Unknown"))
				elif should_reannounce:
					_log("Re-announcing Client Offer to find Host...")
					
				_announce_to_tracker(ws)
				
				if not ws.has_meta("announced"):
					ws.set_meta("announced", true)
					signaling_connected.emit()
				
			while ws.get_available_packet_count() > 0:
				_parse_packet(ws.get_packet())
				
	if not any_open and not _sockets.is_empty() and _sockets.all(func(w): return w.get_ready_state() == WebSocketPeer.STATE_CLOSED):
		_log("All trackers closed. Signaling Disconnected.")
		signaling_disconnected.emit()

func _announce_to_tracker(ws: WebSocketPeer) -> void:
	var offers := []
	if not _is_server and not _client_offer_sdp.is_empty():
		if _client_offer_id.is_empty():
			_client_offer_id = _generate_hash()
			
		offers.append({
			"offer": { "type": "offer", "sdp": _client_offer_sdp },
			"offer_id": _client_offer_id
		})
		_log("Announcing to tracker WITH Client Offer.")
	else:
		_log("Announcing to tracker without offer.")

	var announce_msg := {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"numwant": 50,
		"offers": offers
	}
	_send_to_socket(ws, announce_msg)

func _parse_packet(packet: PackedByteArray) -> void:
	var json_string := packet.get_string_from_utf8()
	var parsed = JSON.parse_string(json_string)
	
	if typeof(parsed) != TYPE_DICTIONARY:
		return
	var data: Dictionary = parsed
	
	if data.has("warning") or data.has("failure reason"):
		_log("TRACKER ERROR: " + json_string)
		return
		
	if data.get("info_hash", "") != _info_hash:
		return
		
	var remote_peer_id: String = data.get("peer_id", "")
	
	if remote_peer_id == _local_peer_id or remote_peer_id.length() != 20:
		return

	var godot_id: int = remote_peer_id.substr(10, 10).to_int()
		
	if not _is_server and _server_wt_id.is_empty():
		_server_wt_id = remote_peer_id
		_log("Client found Server WT_ID: " + _server_wt_id.substr(0, 6) + "...")
		_flush_candidates()
		
	if not webrtc_peer.has_peer(godot_id):
		_log("Discovered New Peer! WT_ID: " + remote_peer_id.substr(0, 6) + "... Godot ID: " + str(godot_id))
		_create_peer_connection(godot_id, remote_peer_id)

	if data.has("offer_id"):
		_peer_map[remote_peer_id + "_offer_id"] = data.get("offer_id")

	if data.has("offer"):
		var payload: Dictionary = data.get("offer")
		if payload.get("type") == "candidate":
			_log("Received Tunneled [CANDIDATE] from Godot ID: " + str(godot_id))
			_handle_candidate(godot_id, payload)
		else:
			_log("Received [OFFER] from Godot ID: " + str(godot_id))
			_handle_offer(godot_id, payload)
			
	elif data.has("answer"):
		var payload: Dictionary = data.get("answer")
		if payload.get("type") == "candidate":
			_log("Received Tunneled [CANDIDATE] from Godot ID: " + str(godot_id))
			_handle_candidate(godot_id, payload)
		else:
			_log("Received [ANSWER] from Godot ID: " + str(godot_id))
			_handle_answer(godot_id, payload)

func _create_peer_connection(godot_id: int, remote_peer_id: String) -> void:
	_log("Initializing WebRTCPeerConnection for Godot ID: " + str(godot_id))
	var peer_connection := WebRTCPeerConnection.new()
	peer_connection.initialize({ "iceServers": ice_servers })
	
	peer_connection.session_description_created.connect(_on_session_description_created.bind(godot_id, remote_peer_id))
	peer_connection.ice_candidate_created.connect(_on_ice_candidate_created.bind(remote_peer_id))
	
	webrtc_peer.add_peer(peer_connection, godot_id) 
	
	if not _is_server and godot_id == 1:
		_log("Client calling create_offer() for Godot ID 1")
		peer_connection.create_offer()

func _handle_offer(godot_id: int, offer_data: Dictionary) -> void:
	if webrtc_peer.has_peer(godot_id):
		_log("Setting Remote Description (OFFER) for Godot ID: " + str(godot_id))
		var connection: WebRTCPeerConnection = webrtc_peer.get_peer(godot_id).get("connection")
		connection.set_remote_description("offer", offer_data.get("sdp", ""))

func _handle_answer(godot_id: int, answer_data: Dictionary) -> void:
	if webrtc_peer.has_peer(godot_id):
		_log("Setting Remote Description (ANSWER) for Godot ID: " + str(godot_id))
		var connection: WebRTCPeerConnection = webrtc_peer.get_peer(godot_id).get("connection")
		connection.set_remote_description("answer", answer_data.get("sdp", ""))

func _handle_candidate(godot_id: int, candidate_data: Dictionary) -> void:
	if webrtc_peer.has_peer(godot_id):
		var connection: WebRTCPeerConnection = webrtc_peer.get_peer(godot_id).get("connection")
		connection.add_ice_candidate(
			candidate_data.get("sdpMid", ""),
			candidate_data.get("sdpMLineIndex", 0),
			candidate_data.get("candidate", "")
		)

func _on_session_description_created(type: String, sdp: String, godot_id: int, remote_peer_id: String) -> void:
	_log("Local SDP Created: [" + type.to_upper() + "] for Godot ID: " + str(godot_id))
	var connection: WebRTCPeerConnection = webrtc_peer.get_peer(godot_id).get("connection")
	connection.set_local_description(type, sdp)
	
	if type == "offer" and not _is_server:
		_client_offer_sdp = sdp
		var pushed_early := false
		for ws in _sockets:
			if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
				_log("Tracker already open. Pushing Client Offer immediately!")
				_announce_to_tracker(ws)
				ws.set_meta("announced", true)
				pushed_early = true
				
		if pushed_early:
			signaling_connected.emit()
			
		return
	
	var msg := {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"to_peer_id": remote_peer_id,
	}
	msg[type] = { "type": type, "sdp": sdp }
	
	if type == "answer" and _peer_map.has(remote_peer_id + "_offer_id"):
		msg["offer_id"] = _peer_map[remote_peer_id + "_offer_id"]
	
	_log("Sending [" + type.to_upper() + "] payload to tracker.")
	_broadcast(msg)

func _on_ice_candidate_created(media: String, index: int, name: String, remote_peer_id: String) -> void:
	var target_peer := remote_peer_id
	if not _is_server:
		if _server_wt_id.is_empty():
			_client_candidate_queue.append({
				"candidate": name,
				"sdpMid": media,
				"sdpMLineIndex": index
			})
			return
		target_peer = _server_wt_id
		
	var msg := {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"to_peer_id": target_peer
	}
	
	var payload := {
		"type": "candidate",
		"candidate": name,
		"sdpMid": media,
		"sdpMLineIndex": index
	}
	
	if _is_server:
		msg["answer"] = payload
		if _peer_map.has(target_peer + "_offer_id"):
			msg["offer_id"] = _peer_map[target_peer + "_offer_id"]
	else:
		msg["offer"] = payload
		msg["offer_id"] = _generate_hash()
		
	_log("Sending Tunneled [CANDIDATE] to Tracker.")
	_broadcast(msg)

func _flush_candidates() -> void:
	if _client_candidate_queue.size() > 0:
		_log("Flushing " + str(_client_candidate_queue.size()) + " queued candidates to Server.")
		
	for c in _client_candidate_queue:
		var msg := {
			"action": "announce",
			"info_hash": _info_hash,
			"peer_id": _local_peer_id,
			"to_peer_id": _server_wt_id,
			"offer_id": _generate_hash(),
			"offer": {
				"type": "candidate",
				"candidate": c.get("candidate"),
				"sdpMid": c.get("sdpMid"),
				"sdpMLineIndex": c.get("sdpMLineIndex")
			}
		}
		_broadcast(msg)
	_client_candidate_queue.clear()

func _broadcast(data: Dictionary) -> void:
	for ws in _sockets:
		_send_to_socket(ws, data)

func _send_to_socket(ws: WebSocketPeer, data: Dictionary) -> void:
	if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
		var json_str := JSON.stringify(data)
		ws.send_text(json_str)

func _get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray:
	return []
