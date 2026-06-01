## [BackendPeer] implementation using WebRTC tracker signaling.
##
## Peers discover each other through WebTorrent compatible tracker servers.
## [method create_host_peer] emits [signal room_created] with the room hash.
## [method create_join_peer] accepts that hash as its address. The WebRTC peer
## machinery lives in [WebRTCSession]; this backend only translates between the
## session and the WebTorrent tracker wire format.
##
## Browser hosts are full peer-to-peer hosts. If the browser throttles an
## unfocused tab, tracker polling and ICE signalling can stall until the tab is
## visible again. Prefer a relay or dedicated host when web-hosted rooms must
## stay reachable while the host tab is backgrounded.
## [codeblock]
## tree.backend = WebRTCBackend.new()
## await tree.host_player(payload)
##
## target.backend = WebRTCBackend.new()
## target.address = room_hash
## await tree.join(target, payload)
## [/codeblock]
@tool
class_name WebRTCBackend
extends BackendPeer

## Emitted when at least one tracker WebSocket connection opens.
signal signaling_connected
## Emitted when all tracker connections close.
signal signaling_disconnected
## Emitted on the host when the room hash is ready to share.
signal room_created(room_id: String)

## WebTorrent compatible tracker URLs used for signaling.
@export var trackers: Array[String] = [
	"wss://tracker.openwebtorrent.com",
	"wss://tracker.webtorrent.dev"
]

## Display name advertised by [WebRTCDirectory] for hosted rooms.
@export var server_name: String = ""

## ICE server definitions passed to each [WebRTCPeerConnection].
@export var ice_servers: Array[Dictionary] = [
	{ "urls": ["stun:stun.l.google.com:19302"] },
	{
		"urls": ["turn:openrelay.metered.ca:80"],
		"username": "openrelayproject",
		"credential": "openrelayproject",
	}
]

var _session: WebRTCSession = null

var _tracker: WebRTCTrackerClient = null
var _is_server := false
var _info_hash := ""
var _local_peer_id := ""
var _server_wt_id := ""
var _client_offer_sdp := ""
var _client_offer_id := ""
var _client_candidate_queue: Array[Dictionary] = []
var _peer_map := {}
var _handled_offers := {}
var _handled_answers := {}
var _local_godot_id := 0
var _announce_timer := 0.0
var _signaling_close_delay := -1.0

## Seconds to keep tracker signaling alive after native WebRTC connects.
const SIGNALING_CLOSE_DELAY := 3.0

## Implements [method BackendPeer.create_host_peer] for a WebRTC room.
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("WebRTCBackend: create_host_peer called.")
	_is_server = true
	_local_godot_id = 1
	_local_peer_id = _generate_peer_id(_local_godot_id)
	_info_hash = _generate_hash()
	_reset_state_vars()

	Netw.dbg.debug(
		"Starting Host. Local WebTorrent ID: %s... Hash: %s",
		[_local_peer_id.substr(0, 6), _info_hash]
	)

	_session = _make_session()
	if _session.create_server() != OK:
		_session = null
		return null
	room_created.emit(_info_hash)

	Netw.dbg.info(
		"Room session ready at `%s` (saved to clipboard).",
		[_info_hash]
	)
	DisplayServer.clipboard_set(_info_hash)

	var tracker_err := _connect_trackers()
	if tracker_err != OK:
		Netw.dbg.error(
			"WebRTC tracker connect failed: %s",
			[error_string(tracker_err)]
		)
		return null
	return _session.webrtc_peer

## Implements [method BackendPeer.create_join_peer] for a WebRTC room hash.
func create_join_peer(
	_tree: MultiplayerTree, server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace(
		"WebRTCBackend: create_join_peer called at %s",
		[server_address]
	)
	_is_server = false
	_local_godot_id = randi() % 1000000 + 2
	_local_peer_id = _generate_peer_id(_local_godot_id)

	if server_address.length() != 20:
		_info_hash = server_address.sha1_text().substr(0, 20)
	else:
		_info_hash = server_address

	_reset_state_vars()

	Netw.dbg.debug(
		"Starting Client. Local Godot ID: %d, Room Hash: %s",
		[_local_godot_id, _info_hash]
	)

	_session = _make_session()
	# create_client opens the offer toward the server, which arrives on
	# signal_out and is announced once a tracker socket is open.
	if _session.create_client(_local_godot_id) != OK:
		_session = null
		return null

	var tracker_err := _connect_trackers()
	if tracker_err != OK:
		Netw.dbg.error(
			"WebRTC tracker connect failed: %s",
			[error_string(tracker_err)]
		)
		return null
	return _session.webrtc_peer

## Implements [method BackendPeer.poll] for tracker and WebRTC state.
func poll(dt: float) -> void:
	if _session:
		_session.poll()

	if _tracker:
		_announce_timer += dt
		if not _is_server and _server_wt_id.is_empty() and _announce_timer > 2.0:
			_announce_timer = 0.0
			Netw.dbg.trace("Re-announcing Client Offer to find Host...")
			_tracker.broadcast(_build_announce())
		_tracker.poll()
		_process_signaling_close_delay(dt)


func _make_session() -> WebRTCSession:
	var session := WebRTCSession.new()
	session.ice_servers = ice_servers
	session.signal_out.connect(_on_session_signal_out)
	session.native_connected.connect(_on_native_connected)
	session.native_disconnected.connect(_on_native_disconnected)
	return session


func _on_native_connected(id: int) -> void:
	Netw.dbg.info(
		"WebRTC Native Connection Established with Godot ID: %d", [id]
	)
	if not _is_server and id == 1:
		Netw.dbg.trace("WebRTC active. Delaying signaling tracker close.")
		_signaling_close_delay = SIGNALING_CLOSE_DELAY


func _on_native_disconnected(id: int) -> void:
	Netw.dbg.info("WebRTC Native Connection Lost with Godot ID: %d", [id])

## Returns the active room hash, or the parent default.
func get_join_address() -> String:
	if not _info_hash.is_empty():
		return _info_hash
	return super.get_join_address()


## Returns a [code]"Room Hash"[/code] [AddressHint].
func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Room Hash",
		"20-char hex",
		"Room identifier copied from the host (also auto-copied to clipboard "
		+ "on host).",
		false,
		false
	)

## Keeps [method BackendPeer.query_server_info] unsupported for room hashes.
##
## WebRTC discovery uses tracker signaling. An [AuthProbeClient] probe would
## need a full ICE handshake, which is too expensive for browser refresh.
func query_server_info(
	_address: String, _timeout: float = 2.0,
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


## Preserves authored WebRTC settings after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	if source is WebRTCBackend:
		trackers = source.trackers.duplicate()
		server_name = source.server_name
		ice_servers = source.ice_servers.duplicate(true)


## Clears tracker sockets, room state, and the active WebRTC session.
func peer_reset_state() -> void:
	Netw.dbg.trace("WebRTCBackend: Resetting Peer State.")
	if _session:
		_session.close()
	_session = null
	if _tracker:
		_tracker.close()
	_tracker = null
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
	_signaling_close_delay = -1.0
	_client_candidate_queue.clear()
	_peer_map.clear()
	_handled_offers.clear()
	_handled_answers.clear()

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
	_tracker = WebRTCTrackerClient.new()
	_tracker.connected.connect(func() -> void: signaling_connected.emit())
	_tracker.disconnected.connect(_on_signaling_lost)
	_tracker.socket_opened.connect(_announce_to)
	_tracker.message_received.connect(_parse_packet)
	return _tracker.connect_to(trackers)

func _on_signaling_lost() -> void:
	Netw.dbg.info("All trackers closed. Signaling Disconnected.")
	signaling_disconnected.emit()

# Sends the first announce when a tracker socket opens.
func _announce_to(ws: WebSocketPeer) -> void:
	_tracker.send(ws, _build_announce())

# Builds the announce payload, attaching the client offer once it exists.
func _build_announce() -> Dictionary:
	var offers := []
	if not _is_server and not _client_offer_sdp.is_empty():
		if _client_offer_id.is_empty():
			_client_offer_id = _generate_hash()
		offers.append({
			"offer": { "type": "offer", "sdp": _client_offer_sdp },
			"offer_id": _client_offer_id
		})
	return {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"numwant": 50,
		"offers": offers
	}

# Decodes a tracker packet into a session.deliver call, dropping replays.
func _parse_packet(data: Dictionary) -> void:
	if data.get("info_hash", "") != _info_hash:
		return

	var remote_peer_id: String = data.get("peer_id", "")

	if remote_peer_id == _local_peer_id or remote_peer_id.length() != 20:
		return

	var godot_id: int = remote_peer_id.substr(10, 10).to_int()

	if not _is_server and _server_wt_id.is_empty():
		_server_wt_id = remote_peer_id
		Netw.dbg.debug("Client found Server WT_ID: %s...",
				[_server_wt_id.substr(0, 6)])
		_flush_candidates()

	if data.has("offer"):
		var payload: Dictionary = data.get("offer")
		if payload.get("type") == "candidate":
			Netw.dbg.debug(
				"Received Tunneled [CANDIDATE] from Godot ID: %d", [godot_id]
			)
			_session.deliver(godot_id, remote_peer_id, "candidate", payload)
		else:
			var offer_id := String(data.get("offer_id", ""))
			if _has_handled_signal(
				_handled_offers, remote_peer_id, offer_id, payload
			):
				Netw.dbg.trace(
					"Ignoring duplicate [OFFER] from Godot ID: %d",
					[godot_id]
				)
				return
			Netw.dbg.debug("Received [OFFER] from Godot ID: %d", [godot_id])
			_peer_map[remote_peer_id + "_offer_id"] = offer_id
			_mark_handled_signal(
				_handled_offers, remote_peer_id, offer_id, payload
			)
			_session.deliver(godot_id, remote_peer_id, "offer", payload)

	elif data.has("answer"):
		var payload: Dictionary = data.get("answer")
		if payload.get("type") == "candidate":
			Netw.dbg.debug(
				"Received Tunneled [CANDIDATE] from Godot ID: %d", [godot_id]
			)
			_session.deliver(godot_id, remote_peer_id, "candidate", payload)
		else:
			var answer_id := String(data.get("offer_id", ""))
			if _has_handled_signal(
				_handled_answers, remote_peer_id, answer_id, payload
			):
				Netw.dbg.trace(
					"Ignoring duplicate [ANSWER] from Godot ID: %d",
					[godot_id]
				)
				return
			Netw.dbg.debug("Received [ANSWER] from Godot ID: %d", [godot_id])
			_mark_handled_signal(
				_handled_answers, remote_peer_id, answer_id, payload
			)
			_session.deliver(godot_id, remote_peer_id, "answer", payload)


func _has_handled_signal(
	handled: Dictionary,
	remote_peer_id: String,
	signal_id: String,
	payload: Dictionary,
) -> bool:
	var key := _signal_key(remote_peer_id, signal_id, payload)
	return handled.has(key)


func _mark_handled_signal(
	handled: Dictionary,
	remote_peer_id: String,
	signal_id: String,
	payload: Dictionary,
) -> void:
	var key := _signal_key(remote_peer_id, signal_id, payload)
	handled[key] = true


func _signal_key(
	remote_peer_id: String,
	signal_id: String,
	payload: Dictionary,
) -> String:
	if not signal_id.is_empty():
		return remote_peer_id + ":" + signal_id
	return remote_peer_id + ":" + String(payload.get("sdp", "")).sha1_text()


# Translates an outbound session signal into a tracker announce.
func _on_session_signal_out(
	_to_multiplayer_id: int,
	to_signaler_id: String,
	kind: String,
	payload: Dictionary,
) -> void:
	match kind:
		"offer":
			# The client's room-directed offer toward the server.
			_client_offer_sdp = String(payload.get("sdp", ""))
			if _tracker and _tracker.has_open():
				Netw.dbg.trace(
					"Tracker already open. Pushing Client Offer immediately!"
				)
				_tracker.broadcast(_build_announce())
		"answer":
			# The server's directed answer to a specific client.
			var msg := _directed_announce(to_signaler_id)
			msg["answer"] = { "type": "answer", "sdp": payload.get("sdp", "") }
			if _peer_map.has(to_signaler_id + "_offer_id"):
				msg["offer_id"] = _peer_map[to_signaler_id + "_offer_id"]
			Netw.dbg.trace("Sending [ANSWER] payload to tracker.")
			_broadcast(msg)
		"candidate":
			_send_candidate(to_signaler_id, payload)


# Tunnels an ICE candidate through the offer/answer slot, queuing the client's
# candidates until it has discovered the server's WebTorrent id.
func _send_candidate(to_signaler_id: String, payload: Dictionary) -> void:
	var target_peer := to_signaler_id
	if not _is_server:
		if _server_wt_id.is_empty():
			_client_candidate_queue.append(payload)
			return
		target_peer = _server_wt_id

	var msg := _directed_announce(target_peer)
	if _is_server:
		msg["answer"] = payload
		if _peer_map.has(target_peer + "_offer_id"):
			msg["offer_id"] = _peer_map[target_peer + "_offer_id"]
	else:
		msg["offer"] = payload
		msg["offer_id"] = _generate_hash()

	Netw.dbg.trace("Sending Tunneled [CANDIDATE] to Tracker.")
	_broadcast(msg)


func _directed_announce(to_peer: String) -> Dictionary:
	return {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"to_peer_id": to_peer,
	}


func _flush_candidates() -> void:
	if _client_candidate_queue.size() > 0:
		Netw.dbg.debug(
			"Flushing %d queued candidates to Server.",
			[_client_candidate_queue.size()]
		)

	for payload in _client_candidate_queue:
		var msg := _directed_announce(_server_wt_id)
		msg["offer_id"] = _generate_hash()
		msg["offer"] = payload
		_broadcast(msg)
	_client_candidate_queue.clear()


func _process_signaling_close_delay(dt: float) -> void:
	if _signaling_close_delay < 0.0:
		return
	_signaling_close_delay -= dt
	if _signaling_close_delay > 0.0:
		return
	_signaling_close_delay = -1.0
	Netw.dbg.trace("Closing delayed WebRTC signaling trackers.")
	if _tracker:
		_tracker.close()
	signaling_disconnected.emit()

func _broadcast(data: Dictionary) -> void:
	if _tracker:
		_tracker.broadcast(data)

## Returns the display name for this backend.
func get_display_name() -> String:
	return "WebRTC"
