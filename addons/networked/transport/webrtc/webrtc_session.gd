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
## ICE is trickled by top-up bundles. The session sends the local description
## immediately, then re-sends the same SDP with its growing candidate list until
## [constant WebRTCPeerConnection.GATHERING_STATE_COMPLETE] or
## [member gather_timeout] ends the top-up window. A client whose native link
## has not opened within [member connect_retry] re-sends the final bundle, up to
## [member max_connect_attempts], without tearing the connection down.
class_name WebRTCSession
extends RefCounted

## Emitted when the native WebRTC link to [param multiplayer_id] opens.
signal native_connected(multiplayer_id: int)
## Emitted when the native WebRTC link to [param multiplayer_id] drops.
signal native_disconnected(multiplayer_id: int)
## Emitted when the client gives up opening the native link.
signal failed(multiplayer_id: int, reason: String)

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

## Seconds a client waits for [signal native_connected] after sending its offer
## bundle before re-sending it to the host. The engine exposes no ICE state, so
## the stall is judged on elapsed wall-clock time.
var connect_retry: float = 8.0

## Maximum offer attempts the client makes toward the host before it leaves the
## final failure to the owning connect budget.
var max_connect_attempts: int = 3

## Wraps each connection in a [ReconnectingPeerConnection] so a transient ICE
## drop does not tear the peer down and re-trigger signaling. Disable to fall
## back to a plain [WebRTCPeerConnection].
var reconnect_masking: bool = true

## Seconds the session waits for ICE gathering to complete before sending the
## final offer/answer top-up, so a gather that never reports complete still
## signals the candidates it managed to collect.
var gather_timeout: float = 6.0

## Minimum seconds between candidate top-up bundles while ICE is gathering.
var topup_interval: float = 0.25

var webrtc_peer: WebRTCMultiplayerPeer = null

## If [code]true[/code], this session is connecting to a local peer on the same
## machine, bypassing TURN configuration to avoid warnings.
var is_local_session: bool = false

var _local_peers: Dictionary = { }
var _is_server := false
# multiplayer_id -> last known opaque signaler address, echoed on outbound.
var _signaler_ids: Dictionary = { }
# multiplayer_id -> bool, true once set_remote_description has landed.
var _remote_desc_set: Dictionary = { }
# multiplayer_id -> Array[Dictionary] of candidates awaiting the remote desc.
var _pending_candidates: Dictionary = { }
# multiplayer_id -> candidate keys already applied to the remote connection.
var _applied_remote_candidates: Dictionary = { }
# multiplayer_id -> { host, srflx, relay } counts gathered this attempt.
var _candidate_stats: Dictionary = { }
# multiplayer_id -> bool, mirrors the native link state.
var _connected_ids: Dictionary = { }
# multiplayer_id -> msec the bundle was last sent at, for the resend clock.
var _attempt_started_ms: Dictionary = { }
# multiplayer_id -> attempt count (1-based).
var _attempts: Dictionary = { }
# multiplayer_id -> { "type", "sdp" } local description held until gathering ends.
var _local_desc: Dictionary = { }
# multiplayer_id -> Array[Dictionary] of locally gathered ICE candidates.
var _local_candidates: Dictionary = { }
# multiplayer_id -> bool, true once any local description bundle has been sent.
var _bundle_sent: Dictionary = { }
# multiplayer_id -> bool, true once newly gathered candidates need a top-up.
var _candidates_dirty: Dictionary = { }
# multiplayer_id -> bool, true once the final gathering top-up has been sent.
var _topups_done: Dictionary = { }
# multiplayer_id -> msec the bundle was last sent at.
var _last_send_ms: Dictionary = { }
# multiplayer_id -> msec at which a still-gathering bundle is sent regardless.
var _gather_deadline_ms: Dictionary = { }
# True once the client logged the give-up summary, so it logs at most once.
var _retry_failed_logged := false
# multiplayer_id -> msec the local offer description was created.
var _offer_sent_ms: Dictionary = { }
# multiplayer_id -> msec the local answer description was created.
var _answer_sent_ms: Dictionary = { }
# multiplayer_id -> msec the native WebRTC connection succeeded.
var _native_connected_ms: Dictionary = { }


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
	_drive_signaling()
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
	var is_local := bool(payload.get("is_local", false))
	_ensure_connection(from_multiplayer_id, from_signaler_id, is_local)
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
	var stats := _candidate_stats.get(multiplayer_id, _empty_stats())
	return (stats as Dictionary).duplicate()


## Returns a diagnostics snapshot for [param multiplayer_id] containing
## connection phase timestamps and candidate statistics.
## [br][br]
## [code]relay_used[/code] is true only if no direct (host or srflx) candidates
## were gathered, meaning a relay was strictly required. Use
## [code]candidates.relay[/code] to see if a relay was gathered/reachable.
func connection_diagnostics(multiplayer_id: int) -> Dictionary:
	var stats := candidate_summary(multiplayer_id)
	var host_count := int(stats.get("host", 0))
	var srflx_count := int(stats.get("srflx", 0))
	var relay_count := int(stats.get("relay", 0))
	var relay_used := relay_count > 0 and host_count == 0 and srflx_count == 0
	return {
		"phases": {
			"offer_ms": _offer_sent_ms.get(multiplayer_id, 0),
			"answer_ms": _answer_sent_ms.get(multiplayer_id, 0),
			"native_ms": _native_connected_ms.get(multiplayer_id, 0),
		},
		"candidates": stats,
		"relay_used": relay_used,
	}


## Starts closing active [WebRTCDataChannel]s before [method close].
##
## Callers that can yield should poll or await a few frames after this method
## so the SCTP stream reset handshake can flush before the peer is released.
func close_channels() -> void:
	if webrtc_peer == null:
		return
	for peer_info in webrtc_peer.get_peers().values():
		_close_peer_channels(peer_info)


## Closes the peer and clears per-remote address state.
func close() -> void:
	if webrtc_peer:
		close_channels()
		webrtc_peer.close()
	webrtc_peer = null
	is_local_session = false
	_local_peers.clear()
	_signaler_ids.clear()
	_remote_desc_set.clear()
	_pending_candidates.clear()
	_applied_remote_candidates.clear()
	_candidate_stats.clear()
	_connected_ids.clear()
	_attempt_started_ms.clear()
	_attempts.clear()
	_local_desc.clear()
	_local_candidates.clear()
	_bundle_sent.clear()
	_candidates_dirty.clear()
	_topups_done.clear()
	_last_send_ms.clear()
	_gather_deadline_ms.clear()
	_retry_failed_logged = false
	_offer_sent_ms.clear()
	_answer_sent_ms.clear()
	_native_connected_ms.clear()


func _bind_peer(peer: WebRTCMultiplayerPeer) -> void:
	peer.peer_connected.connect(_on_peer_connected)
	peer.peer_disconnected.connect(_on_peer_disconnected)


# Creates the WebRTCPeerConnection for multiplayer_id if absent. The client
# side calls create_offer toward the server (id 1).
func _ensure_connection(
		multiplayer_id: int,
		signaler_id: String,
		is_local: bool = false,
) -> void:
	if webrtc_peer.has_peer(multiplayer_id):
		return
	if not signaler_id.is_empty():
		_signaler_ids[multiplayer_id] = signaler_id
	_attempts[multiplayer_id] = 1
	_open_connection(multiplayer_id, is_local)


# Builds a fresh WebRTCPeerConnection for multiplayer_id and arms the attempt
# clock. The client side offers toward the server (id 1).
func _open_connection(multiplayer_id: int, is_local: bool = false) -> void:
	Netw.dbg.trace(
		"WebRTCSession: opening WebRTCPeerConnection for id %d (attempt %d).",
		[multiplayer_id, _attempts.get(multiplayer_id, 1)],
	)
	_local_peers[multiplayer_id] = is_local
	_remote_desc_set[multiplayer_id] = false
	_pending_candidates[multiplayer_id] = []
	_applied_remote_candidates[multiplayer_id] = { }
	_candidate_stats[multiplayer_id] = _empty_stats()
	_attempt_started_ms[multiplayer_id] = Time.get_ticks_msec()
	_local_desc.erase(multiplayer_id)
	_local_candidates[multiplayer_id] = []
	_offer_sent_ms.erase(multiplayer_id)
	_answer_sent_ms.erase(multiplayer_id)
	_native_connected_ms.erase(multiplayer_id)

	_bundle_sent[multiplayer_id] = false
	_candidates_dirty[multiplayer_id] = false
	_topups_done[multiplayer_id] = false
	_last_send_ms[multiplayer_id] = 0
	_gather_deadline_ms.erase(multiplayer_id)
	var connection: WebRTCPeerConnection = (
			ReconnectingPeerConnection.new() if reconnect_masking
			else WebRTCPeerConnection.new()
	)
	var active_servers = [] if (is_local or is_local_session) else ice_servers
	connection.initialize({ "iceServers": active_servers })
	connection.session_description_created.connect(
		_on_session_description_created.bind(multiplayer_id),
	)
	connection.ice_candidate_created.connect(
		_on_ice_candidate_created.bind(multiplayer_id),
	)
	webrtc_peer.add_peer(connection, multiplayer_id)
	if not _is_server and multiplayer_id == 1:
		connection.create_offer()


# Re-sends the host link's offer bundle when it has not opened within
# connect_retry, bounded by max_connect_attempts. The connection is kept alive,
# so a slow negotiation keeps its progress and only the lossy signaling is
# nudged. The bundle is identical each time, so a host that already has it just
# deduplicates the resend.
func _maybe_retry() -> void:
	if not webrtc_peer.has_peer(1) or _connected_ids.has(1):
		return
	if not _bundle_sent.get(1, false):
		# Still gathering the first bundle; there is nothing to re-send yet.
		return
	var started := int(_attempt_started_ms.get(1, 0))
	if started == 0:
		return
	if Time.get_ticks_msec() - started < int(connect_retry * 1000.0):
		return
	if int(_attempts.get(1, 1)) >= max_connect_attempts:
		if not _retry_failed_logged:
			_retry_failed_logged = true
			_log_attempt_summary(1, "failed")
			failed.emit(1, _failure_reason(1))
		return
	_attempts[1] = int(_attempts.get(1, 1)) + 1
	Netw.dbg.trace(
		"WebRTCSession: re-sending offer to id 1 (attempt %d).",
		[_attempts[1]],
	)
	_send_bundle(1)


func _handle_offer(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	# Never hand the engine SDP-less payload: a misrouted candidate would
	# otherwise crash the browser with an empty-description parse error.
	if String(payload.get("sdp", "")).is_empty():
		Netw.dbg.debug(
			"WebRTCSession dropped SDP-less offer for id %d.",
			[multiplayer_id],
		)
		return
	if not _remote_desc_set.get(multiplayer_id, false):
		var connection := _connection(multiplayer_id)
		var err := connection.set_remote_description("offer", payload.get("sdp", ""))
		if err != OK:
			Netw.dbg.debug(
				"WebRTCSession ignored stale offer for id %d: %s",
				[multiplayer_id, error_string(err)],
			)
			return
		_remote_desc_set[multiplayer_id] = true
	_apply_bundled_candidates(multiplayer_id, payload)
	_flush_pending_candidates(multiplayer_id)


func _handle_answer(multiplayer_id: int, payload: Dictionary) -> void:
	if not webrtc_peer.has_peer(multiplayer_id):
		return
	# Never hand the engine SDP-less payload: a misrouted candidate would
	# otherwise crash the browser with an empty-description parse error.
	if String(payload.get("sdp", "")).is_empty():
		Netw.dbg.debug(
			"WebRTCSession dropped SDP-less answer for id %d.",
			[multiplayer_id],
		)
		return
	if not _remote_desc_set.get(multiplayer_id, false):
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
	_apply_bundled_candidates(multiplayer_id, payload)
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


func _close_peer_channels(peer_info: Dictionary) -> void:
	var channels: Array = peer_info.get("channels", [])
	for item in channels:
		var channel := item as WebRTCDataChannel
		if channel and channel.get_ready_state() < WebRTCDataChannel.STATE_CLOSING:
			channel.close()


func _add_candidate(multiplayer_id: int, payload: Dictionary) -> void:
	var key := _candidate_key(payload)
	var applied: Dictionary = _applied_remote_candidates.get(multiplayer_id, { })
	if applied.has(key):
		return
	applied[key] = true
	_applied_remote_candidates[multiplayer_id] = applied
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
	if type == "offer":
		_offer_sent_ms[multiplayer_id] = Time.get_ticks_msec()
	elif type == "answer":
		_answer_sent_ms[multiplayer_id] = Time.get_ticks_msec()
	_local_desc[multiplayer_id] = { "type": type, "sdp": sdp }
	_bundle_sent[multiplayer_id] = true

	_candidates_dirty[multiplayer_id] = false
	_topups_done[multiplayer_id] = false
	_gather_deadline_ms[multiplayer_id] = (
			Time.get_ticks_msec() + int(gather_timeout * 1000.0)
	)
	_send_bundle(multiplayer_id)


func _on_ice_candidate_created(
		media: String,
		index: int,
		name: String,
		multiplayer_id: int,
) -> void:
	_account_candidate(multiplayer_id, name)
	if not _local_candidates.has(multiplayer_id):
		_local_candidates[multiplayer_id] = []
	(_local_candidates[multiplayer_id] as Array).append(
		{
			"type": "candidate",
			"candidate": name,
			"sdpMid": media,
			"sdpMLineIndex": index,
		},
	)
	_candidates_dirty[multiplayer_id] = true


# Coalesces growing candidate bundles while ICE gathers, then sends one final
# bundle when the gather window ends.
func _drive_signaling() -> void:
	for multiplayer_id: int in _local_desc.keys():
		if _topups_done.get(multiplayer_id, false):
			continue
		if not webrtc_peer.has_peer(multiplayer_id):
			continue
		var complete := _connection(multiplayer_id).get_gathering_state() \
				== WebRTCPeerConnection.GATHERING_STATE_COMPLETE
		var timed_out := Time.get_ticks_msec() \
				>= int(_gather_deadline_ms.get(multiplayer_id, 0))
		if complete or timed_out:
			_candidates_dirty[multiplayer_id] = false
			_topups_done[multiplayer_id] = true
			_send_bundle(multiplayer_id)
			continue
		if not _candidates_dirty.get(multiplayer_id, false):
			continue
		var elapsed_ms := Time.get_ticks_msec() \
				- int(_last_send_ms.get(multiplayer_id, 0))
		if elapsed_ms < int(topup_interval * 1000.0):
			continue
		_candidates_dirty[multiplayer_id] = false
		_send_bundle(multiplayer_id)


# Emits the buffered description with all gathered candidates as one payload and
# arms the resend clock from this moment.
func _send_bundle(multiplayer_id: int) -> void:
	var desc: Dictionary = _local_desc.get(multiplayer_id, { })
	if desc.is_empty():
		return
	var now := Time.get_ticks_msec()
	_attempt_started_ms[multiplayer_id] = now
	_last_send_ms[multiplayer_id] = now
	var candidates: Array = _local_candidates.get(multiplayer_id, [])
	Netw.dbg.trace(
		"WebRTCSession: sending %s bundle for id %d (%d candidate(s)).",
		[String(desc["type"]), multiplayer_id, candidates.size()],
	)
	var is_local: bool = (
			is_local_session
			or bool(_local_peers.get(multiplayer_id, false))
	)
	signal_out.emit(
		multiplayer_id,
		String(_signaler_ids.get(multiplayer_id, "")),
		String(desc["type"]),
		{
			"type": desc["type"],
			"sdp": desc["sdp"],
			"candidates": candidates.duplicate(),
			"is_local": is_local,
		},
	)


# Applies every ICE candidate carried inside an offer or answer bundle now that
# its remote description has landed.
func _apply_bundled_candidates(multiplayer_id: int, payload: Dictionary) -> void:
	var candidates: Variant = payload.get("candidates", [])
	if typeof(candidates) != TYPE_ARRAY:
		return
	for candidate: Variant in candidates:
		if typeof(candidate) == TYPE_DICTIONARY:
			_add_candidate(multiplayer_id, candidate)


func _candidate_key(payload: Dictionary) -> String:
	return "%s:%d:%s" % [
		String(payload.get("sdpMid", "")),
		int(payload.get("sdpMLineIndex", 0)),
		String(payload.get("candidate", "")),
	]


func _on_peer_connected(multiplayer_id: int) -> void:
	_connected_ids[multiplayer_id] = true
	_native_connected_ms[multiplayer_id] = Time.get_ticks_msec()
	var stats: Dictionary = _candidate_stats.get(multiplayer_id, _empty_stats())

	Netw.dbg.debug(
		"WebRTCSession: native link up id %d (host=%d srflx=%d relay=%d).",
		[multiplayer_id, stats.host, stats.srflx, stats.relay],
	)
	native_connected.emit(multiplayer_id)


func _on_peer_disconnected(multiplayer_id: int) -> void:
	_connected_ids.erase(multiplayer_id)
	native_disconnected.emit(multiplayer_id)


# Tallies a gathered candidate by ICE type so failed attempts can be classed.
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
	var is_local := (
			is_local_session
			or bool(_local_peers.get(multiplayer_id, false))
	)
	if stats.relay == 0 and not is_local:
		Netw.dbg.warn(
			"WebRTCSession: id %d %s with no relay candidate gathered "
			+ "(host=%d srflx=%d); TURN may be unreachable.",
			[multiplayer_id, why, stats.host, stats.srflx],
		)
	else:
		var action := "retrying signaling" if why != "failed" else "giving up"
		Netw.dbg.debug(
			"WebRTCSession: id %d %s (host=%d srflx=%d relay=%d); "
			+ "relay reachable, %s.",
			[multiplayer_id, why, stats.host, stats.srflx, stats.relay, action],
		)


func _empty_stats() -> Dictionary:
	return { "host": 0, "srflx": 0, "relay": 0 }


func _failure_reason(multiplayer_id: int) -> String:
	if not _remote_desc_set.get(multiplayer_id, false):
		return "HOST_UNRESPONSIVE"
	var stats: Dictionary = _candidate_stats.get(multiplayer_id, _empty_stats())
	if int(stats.get("relay", 0)) == 0:
		return "TURN_UNREACHABLE"
	return "NAT_TRAVERSAL_FAILED"


func _connection(multiplayer_id: int) -> WebRTCPeerConnection:
	return webrtc_peer.get_peer(multiplayer_id).get("connection")
