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
## each frame: session.poll(delta)
## [/codeblock]
## [br][br]
## A candidate may arrive before the SDP that anchors it, so each connection
## holds a pending-candidate queue that [method deliver] flushes once
## [method WebRTCPeerConnection.set_remote_description] lands. A client whose
## native link to the host has not opened within [member connect_retry] rebuilds
## the connection with a fresh offer, up to [member max_connect_attempts], which
## a tracker signaler turns into a fresh rendezvous.
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

## Seconds a client waits for [signal native_connected] before rebuilding the
## connection to the host with a fresh offer. The engine exposes no ICE state,
## so the stall is judged on elapsed wall-clock time.
var connect_retry: float = 4.0

## Maximum offer attempts the client makes toward the host before it leaves the
## final failure to the owning connect budget.
var max_connect_attempts: int = 3

var webrtc_peer: WebRTCMultiplayerPeer = null

var _is_server := false
# multiplayer_id -> last known opaque signaler address, echoed on outbound.
var _signaler_ids: Dictionary = { }
# multiplayer_id -> bool, true once set_remote_description has landed.
var _remote_desc_set: Dictionary = { }
# multiplayer_id -> Array[Dictionary] of candidates awaiting the remote desc.
var _pending_candidates: Dictionary = { }
# multiplayer_id -> { host, srflx, relay } counts gathered this attempt.
var _candidate_stats: Dictionary = { }
# multiplayer_id -> bool, mirrors the native link state.
var _connected_ids: Dictionary = { }
# multiplayer_id -> msec the current attempt opened at.
var _attempt_started_ms: Dictionary = { }
# multiplayer_id -> attempt count (1-based).
var _attempts: Dictionary = { }


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


## Polls the underlying [WebRTCMultiplayerPeer] and drives client retry.
func poll(dt: float = 0.0) -> void:
	if webrtc_peer == null:
		return
	webrtc_peer.poll()
	if not _is_server:
		_maybe_retry()


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


## Returns the ICE candidate counts gathered for [param multiplayer_id] this
## attempt as [code]{ host, srflx, relay }[/code].
##
## A completed attempt with [code]relay == 0[/code] points at an unreachable
## TURN relay rather than a signaling fault.
func candidate_summary(multiplayer_id: int) -> Dictionary:
	return (_candidate_stats.get(multiplayer_id, _empty_stats()) as Dictionary).duplicate()


## Closes the peer and clears per-remote address state.
func close() -> void:
	if webrtc_peer:
		webrtc_peer.close()
	webrtc_peer = null
	_signaler_ids.clear()
	_remote_desc_set.clear()
	_pending_candidates.clear()
	_candidate_stats.clear()
	_connected_ids.clear()
	_attempt_started_ms.clear()
	_attempts.clear()


func _bind_peer(peer: WebRTCMultiplayerPeer) -> void:
	peer.peer_connected.connect(_on_peer_connected)
	peer.peer_disconnected.connect(_on_peer_disconnected)


# Creates the WebRTCPeerConnection for multiplayer_id if absent. The client
# side calls create_offer toward the server (id 1).
func _ensure_connection(multiplayer_id: int, signaler_id: String) -> void:
	if webrtc_peer.has_peer(multiplayer_id):
		return
	if not signaler_id.is_empty():
		_signaler_ids[multiplayer_id] = signaler_id
	_attempts[multiplayer_id] = 1
	_open_connection(multiplayer_id)


# Builds a fresh WebRTCPeerConnection for multiplayer_id and arms the attempt
# clock. The client side offers toward the server (id 1).
func _open_connection(multiplayer_id: int) -> void:
	Netw.dbg.trace(
		"WebRTCSession: opening WebRTCPeerConnection for id %d (attempt %d).",
		[multiplayer_id, _attempts.get(multiplayer_id, 1)],
	)
	_remote_desc_set[multiplayer_id] = false
	_pending_candidates[multiplayer_id] = []
	_candidate_stats[multiplayer_id] = _empty_stats()
	_attempt_started_ms[multiplayer_id] = Time.get_ticks_msec()
	var connection := WebRTCPeerConnection.new()
	connection.initialize({ "iceServers": ice_servers })
	connection.session_description_created.connect(
		_on_session_description_created.bind(multiplayer_id),
	)
	connection.ice_candidate_created.connect(
		_on_ice_candidate_created.bind(multiplayer_id),
	)
	webrtc_peer.add_peer(connection, multiplayer_id)
	if not _is_server and multiplayer_id == 1:
		connection.create_offer()


# Rebuilds a stalled connection in place under the same multiplayer_id, so the
# client re-offers and a tracker signaler mints a fresh rendezvous.
func _rebuild_connection(multiplayer_id: int) -> void:
	_log_attempt_summary(multiplayer_id, "stalled")
	_attempts[multiplayer_id] = int(_attempts.get(multiplayer_id, 1)) + 1
	_connected_ids.erase(multiplayer_id)
	if webrtc_peer.has_peer(multiplayer_id):
		webrtc_peer.remove_peer(multiplayer_id)
	_open_connection(multiplayer_id)


# Rebuilds the host side for a client whose retry sent a fresh offer.
func _restart_remote(multiplayer_id: int) -> void:
	Netw.dbg.trace(
		"WebRTCSession: restarting connection for id %d on renegotiated offer.",
		[multiplayer_id],
	)
	if webrtc_peer.has_peer(multiplayer_id):
		webrtc_peer.remove_peer(multiplayer_id)
	_open_connection(multiplayer_id)


# Tears down and re-offers the host link when it has not opened within
# connect_retry, bounded by max_connect_attempts.
func _maybe_retry() -> void:
	if not webrtc_peer.has_peer(1) or _connected_ids.has(1):
		return
	var started := int(_attempt_started_ms.get(1, 0))
	if started == 0:
		return
	if Time.get_ticks_msec() - started < int(connect_retry * 1000.0):
		return
	if int(_attempts.get(1, 1)) >= max_connect_attempts:
		# Budget exhausted; let the owning connect timeout report the failure.
		return
	_rebuild_connection(1)


func _handle_offer(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	# A fresh offer on an unconnected link is a client retry: restart the host
	# side so the engine accepts the renegotiation cleanly.
	if _remote_desc_set.get(multiplayer_id, false) \
			and not _connected_ids.has(multiplayer_id):
		_restart_remote(multiplayer_id)
	var connection := _connection(multiplayer_id)
	var err := connection.set_remote_description("offer", payload.get("sdp", ""))
	if err != OK:
		Netw.dbg.debug(
			"WebRTCSession ignored stale offer for id %d: %s",
			[multiplayer_id, error_string(err)],
		)
		return
	_remote_desc_set[multiplayer_id] = true
	_flush_pending_candidates(multiplayer_id)


func _handle_answer(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	var err := _connection(multiplayer_id).set_remote_description(
		"answer",
		payload.get("sdp", ""),
	)
	if err != OK:
		Netw.dbg.debug(
			"WebRTCSession ignored stale answer for id %d: %s",
			[multiplayer_id, error_string(err)],
		)
		return
	_remote_desc_set[multiplayer_id] = true
	_flush_pending_candidates(multiplayer_id)


func _handle_candidate(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	if not _remote_desc_set.get(multiplayer_id, false):
		# Anchor SDP not applied yet; hold the candidate until it lands.
		(_pending_candidates.get(multiplayer_id, []) as Array).append(payload)
		return
	_add_candidate(multiplayer_id, payload)


func _flush_pending_candidates(multiplayer_id: int) -> void:
	var queue: Array = _pending_candidates.get(multiplayer_id, [])
	if queue.is_empty():
		return
	Netw.dbg.trace(
		"WebRTCSession: flushing %d queued candidate(s) for id %d.",
		[queue.size(), multiplayer_id],
	)
	for payload in queue:
		_add_candidate(multiplayer_id, payload)
	queue.clear()


func _add_candidate(multiplayer_id: int, payload: Dictionary) -> void:
	_connection(multiplayer_id).add_ice_candidate(
		payload.get("sdpMid", ""),
		payload.get("sdpMLineIndex", 0),
		payload.get("candidate", ""),
	)


func _on_session_description_created(
		type: String,
		sdp: String,
		multiplayer_id: int,
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
		media: String,
		index: int,
		name: String,
		multiplayer_id: int,
) -> void:
	_account_candidate(multiplayer_id, name)
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


func _on_peer_connected(multiplayer_id: int) -> void:
	_connected_ids[multiplayer_id] = true
	var stats: Dictionary = _candidate_stats.get(multiplayer_id, _empty_stats())
	Netw.dbg.debug(
		"WebRTCSession: native link up id %d (host=%d srflx=%d relay=%d).",
		[multiplayer_id, stats.host, stats.srflx, stats.relay],
	)
	native_connected.emit(multiplayer_id)


func _on_peer_disconnected(multiplayer_id: int) -> void:
	_connected_ids.erase(multiplayer_id)
	native_disconnected.emit(multiplayer_id)


# Tallies a gathered candidate by ICE type so a failed attempt can be classed
# as a TURN/STUN problem (no relay) or a swarm/signaling problem (relay present).
func _account_candidate(multiplayer_id: int, candidate: String) -> void:
	var stats: Dictionary = _candidate_stats.get(multiplayer_id, _empty_stats())
	if " typ relay" in candidate:
		stats.relay += 1
	elif " typ srflx" in candidate:
		stats.srflx += 1
	elif " typ host" in candidate:
		stats.host += 1
	_candidate_stats[multiplayer_id] = stats


func _log_attempt_summary(multiplayer_id: int, why: String) -> void:
	var stats: Dictionary = _candidate_stats.get(multiplayer_id, _empty_stats())
	if stats.relay == 0:
		Netw.dbg.warn(
			"WebRTCSession: id %d %s with no relay candidate gathered "
			+ "(host=%d srflx=%d); TURN may be unreachable.",
			[multiplayer_id, why, stats.host, stats.srflx],
		)
	else:
		Netw.dbg.debug(
			"WebRTCSession: id %d %s (host=%d srflx=%d relay=%d); "
			+ "relay reachable, retrying signaling.",
			[multiplayer_id, why, stats.host, stats.srflx, stats.relay],
		)


func _empty_stats() -> Dictionary:
	return { "host": 0, "srflx": 0, "relay": 0 }


func _connection(multiplayer_id: int) -> WebRTCPeerConnection:
	return webrtc_peer.get_peer(multiplayer_id).get("connection")
