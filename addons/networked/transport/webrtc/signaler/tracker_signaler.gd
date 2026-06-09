## [WebRTCSignaler] over WebTorrent compatible trackers, no signaling server.
##
## A tracker is a content addressed rendezvous keyed by a 20 char
## [code]info_hash[/code]. This signaler owns that addressing entirely. It
## embeds the engine [code]multiplayer_id[/code] into the 20 char WebTorrent
## [code]peer_id[/code] and extracts it back on receipt, so a [WebRTCSession]
## never sees a tracker id.
## [br][br]
## A public tracker normalizes the [code]offers[/code] matchmaking array and
## strips any field it does not recognize, so ICE candidates cannot ride it. This
## signaler instead derives the host's [code]peer_id[/code] from the
## [code]info_hash[/code], so a client can address the host directly and send its
## offer through the directed [code]answer[/code] slot the tracker forwards
## verbatim. Both the offer and the answer carry their full candidate bundle in
## that one message, matching the non-trickle [WebRTCSession].
## [codeblock]
## TrackerSignaler.new(trackers)
## open("", 1)                     # host: generates info_hash
## open(room_hash, client_id)      # client: normalizes the hash
##
## host peer_id   = info_hash[0..10] + "0000000001"  (derivable by clients)
## client peer_id = rand10          + multiplayer_id.pad_zeros(10)
## multiplayer_id = peer_id.substr(10, 10).to_int()
##
## client offer -> answer slot { type:"offer",  sdp, candidates:[...] }
## host answer  -> answer slot { type:"answer", sdp, candidates:[...] }
## [/codeblock]
class_name TrackerSignaler
extends WebRTCSignaler

## Seconds to keep tracker signaling alive after the native WebRTC link is up.
const SIGNALING_CLOSE_DELAY := 3.0

var trackers: Array[String] = []

var _tracker: WebTorrentTrackerClient = null
var _is_server := false
var _info_hash := ""
var _local_peer_id := ""
var _local_godot_id := 0
# The host's derived address, so a client sends its offer straight to it.
var _server_wt_id := ""
var _native_up := false

# remote_peer_id:sha1(sdp) -> true, so a re-sent offer or answer is handled once.
var _handled_offers := { }
var _handled_answers := { }
var _announce_timer := 0.0
var _signaling_close_delay := -1.0
# Tracker-requested minimum seconds between announces, used as the presence
# re-announce floor so the loop never outpaces a tracker's rate limit. 0 keeps
# the built-in floor.
var _min_announce_period := 0.0


func _init(p_trackers: Array[String] = []) -> void:
	trackers = p_trackers


func open(p_room_id: String, local_multiplayer_id: int) -> Error:
	_local_godot_id = local_multiplayer_id
	_is_server = local_multiplayer_id == 1
	if _is_server:
		# The signaler owns room-id generation; room semantics are its own.
		_info_hash = _generate_hash()
	elif p_room_id.length() != 20:
		_info_hash = p_room_id.sha1_text().substr(0, 20)
	else:
		_info_hash = p_room_id
	# The peer_id derives from the room hash for the host, so it must be known.
	_local_peer_id = _generate_peer_id(local_multiplayer_id)
	if not _is_server:
		# The host's address is derived, so the client offers to it directly
		# instead of waiting for tracker matchmaking to introduce them.
		_server_wt_id = _host_peer_id()
	Netw.dbg.debug(
		"TrackerSignaler: opening room %s as id %d (peer_id %s...).",
		[_info_hash, local_multiplayer_id, _local_peer_id.substr(0, 6)],
	)
	return _connect_trackers()


func room_id() -> String:
	return _info_hash


func local_signaler_id() -> String:
	return _local_peer_id


func poll(dt: float) -> void:
	if _tracker == null:
		return
	_tracker.poll()
	_announce_timer += dt
	_drive_presence()
	_process_signaling_close_delay(dt)


func close() -> void:
	if _tracker:
		_send_stop()
		_tracker.close()
	_tracker = null


# Announces departure so the tracker drops this peer_id from the swarm at once,
# instead of leaving a stale rendezvous for others to keep dialing.
func _send_stop() -> void:
	_broadcast(
		{
			"action": "announce",
			"info_hash": _info_hash,
			"peer_id": _local_peer_id,
			"event": "stopped",
		},
	)


func on_session_connected(multiplayer_id: int) -> void:
	# Wind down signaling once the native link to the host is up.
	if not _is_server and multiplayer_id == 1:
		Netw.dbg.trace("TrackerSignaler: native link up, delaying close.")
		_native_up = true
		_signaling_close_delay = SIGNALING_CLOSE_DELAY


# Relays an outbound offer or answer as a directed message in the answer slot the
# tracker forwards verbatim. Candidates already ride bundled in the payload, so
# the candidate kind has nothing left to send.
func send(
		_to_multiplayer_id: int,
		to_signaler_id: String,
		kind: String,
		payload: Dictionary,
) -> void:
	match kind:
		"offer":
			# The client addresses the host by its derived peer_id.
			_send_directed(_server_wt_id, "offer", payload)
		"answer":
			# The host answers the client it learned from the inbound offer.
			_send_directed(to_signaler_id, "answer", payload)


# Sends one offer or answer, with its candidate bundle, directed at to_peer. The
# real kind rides the payload "type" because both share the tracker's answer
# slot, the only directed channel a public tracker relays untouched.
func _send_directed(to_peer: String, type: String, payload: Dictionary) -> void:
	if to_peer.is_empty():
		return
	var candidates: Variant = payload.get("candidates", [])
	if typeof(candidates) != TYPE_ARRAY:
		candidates = []
	Netw.dbg.trace(
		"TrackerSignaler: directed [%s] to %s... (%d candidate(s)).",
		[type.to_upper(), to_peer.substr(0, 6), (candidates as Array).size()],
	)
	_broadcast(
		{
			"action": "announce",
			"info_hash": _info_hash,
			"peer_id": _local_peer_id,
			"to_peer_id": to_peer,
			"offer_id": "0",
			"answer": {
				"type": type,
				"sdp": String(payload.get("sdp", "")),
				"candidates": candidates,
			},
		},
	)


# Re-announces presence so the tracker keeps this peer routable for directed
# offers and answers, until the native link winds signaling down.
func _drive_presence() -> void:
	if _native_up:
		return
	if _announce_timer < _reannounce_period():
		return
	_announce_timer = 0.0
	if _tracker.has_open():
		_tracker.broadcast(_build_presence())


# -- Tracker plumbing -------------------------------------------------------


# Builds the tracker transport. A test overrides this to record announces.
func _make_tracker() -> WebTorrentTrackerClient:
	return WebTorrentTrackerClient.new()


func _connect_trackers() -> Error:
	_tracker = _make_tracker()
	_tracker.connected.connect(func() -> void: ready.emit())
	_tracker.disconnected.connect(_on_signaling_lost)
	_tracker.socket_opened.connect(_announce_to)
	_tracker.message_received.connect(_parse_packet)
	var err := _tracker.connect_to(trackers)
	if err != OK:
		lost.emit()
	return err


func _on_signaling_lost() -> void:
	Netw.dbg.info("TrackerSignaler: all trackers closed.")
	lost.emit()


# Sends the first presence announce when a tracker socket opens.
func _announce_to(ws: WebSocketPeer) -> void:
	_tracker.send(ws, _build_presence())


# A bare announce that registers this peer_id in the swarm so the tracker can
# route directed messages to it. It carries no offers, so it never enters the
# matchmaking array the tracker would normalize.
func _build_presence() -> Dictionary:
	return {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"numwant": 1,
	}


# Decodes a tracker packet and reports it up as a received() signal, dropping
# tracker replays and re-sends.
func _parse_packet(data: Dictionary) -> void:
	if data.get("info_hash", "") != _info_hash:
		return

	# Tracker announce replies carry no peer_id but advertise the minimum
	# announce spacing, so capture it before the peer_id filter drops them.
	_capture_announce_interval(data)

	var remote_peer_id: String = data.get("peer_id", "")
	if remote_peer_id == _local_peer_id or remote_peer_id.length() != 20:
		return

	var godot_id: int = remote_peer_id.substr(10, 10).to_int()

	# Offers and answers both arrive in the directed answer slot.
	var payload: Variant = data.get("answer")
	if typeof(payload) != TYPE_DICTIONARY:
		return
	var dict: Dictionary = payload

	var type: String = dict.get("type", "")
	_trace_relayed_payload("answer", type, dict)
	match type:
		"offer":
			_handle_inbound(godot_id, remote_peer_id, "offer", dict, _handled_offers)
		"answer":
			_handle_inbound(godot_id, remote_peer_id, "answer", dict, _handled_answers)


# Reports one inbound offer or answer once, deduped by its SDP so the sender's
# reliability re-sends do not reprocess it.
func _handle_inbound(
		godot_id: int,
		remote_peer_id: String,
		kind: String,
		payload: Dictionary,
		handled: Dictionary,
) -> void:
	var key := remote_peer_id + ":" + String(payload.get("sdp", "")).sha1_text()
	if handled.has(key):
		Netw.dbg.trace(
			"TrackerSignaler: ignoring duplicate [%s] from id %d.",
			[kind.to_upper(), godot_id],
		)
		return
	handled[key] = true
	Netw.dbg.debug(
		"TrackerSignaler: [%s] from id %d (%d candidate(s)).",
		[kind.to_upper(), godot_id, _candidate_count(payload)],
	)
	received.emit(godot_id, remote_peer_id, kind, payload)


func _candidate_count(payload: Dictionary) -> int:
	var candidates: Variant = payload.get("candidates", [])
	return (candidates as Array).size() if typeof(candidates) == TYPE_ARRAY else 0


# Phase 0 probe: reports what a tracker actually relays, to confirm bundled ICE
# candidates (especially TURN relay ones) survive the directed answer slot.
# Remove once the directed-bundle model is confirmed on real trackers.
func _trace_relayed_payload(slot: String, type: String, payload: Dictionary) -> void:
	var sdp_len := String(payload.get("sdp", "")).length()
	var bundled: Variant = payload.get("candidates", null)
	var present := typeof(bundled) == TYPE_ARRAY
	var total := (bundled as Array).size() if present else 0
	var relay := 0
	if present:
		for candidate: Variant in bundled:
			if typeof(candidate) == TYPE_DICTIONARY:
				if " typ relay" in String((candidate as Dictionary).get("candidate", "")):
					relay += 1
	Netw.dbg.trace(
		"TrackerSignaler: RELAYED slot=%s type=%s sdp_len=%d candidates=%s (%d total, %d relay).",
		[slot, type, sdp_len, "present" if present else "STRIPPED", total, relay],
	)


# Reads the tracker's advertised minimum announce spacing so the presence loop
# can honor it. Tracker dialects spell the key a few ways.
func _capture_announce_interval(data: Dictionary) -> void:
	var raw: Variant = data.get("min interval", data.get("min_interval", null))
	if typeof(raw) in [TYPE_INT, TYPE_FLOAT]:
		_min_announce_period = float(raw)


# The presence re-announce period: fast by default, but never faster than a
# rate-limiting tracker's advertised minimum.
func _reannounce_period() -> float:
	return maxf(2.0, _min_announce_period)


func _process_signaling_close_delay(dt: float) -> void:
	if _signaling_close_delay < 0.0:
		return
	_signaling_close_delay -= dt
	if _signaling_close_delay > 0.0:
		return
	_signaling_close_delay = -1.0
	# Intentional wind-down after the native link is up, so no lost() here.
	Netw.dbg.trace("TrackerSignaler: closing delayed signaling trackers.")
	if _tracker:
		_send_stop()
		_tracker.close()
	_tracker = null


func _broadcast(data: Dictionary) -> void:
	if _tracker:
		_tracker.broadcast(data)

# -- Id generation ----------------------------------------------------------


func _generate_hash() -> String:
	var chars := "0123456789abcdef"
	var hash_str := ""
	for i in 20:
		hash_str += chars[randi() % chars.length()]
	return hash_str


func _generate_peer_id(godot_id: int) -> String:
	# The host's id derives from the room hash so a client can address it
	# directly; a client keeps a random prefix so two clients never collide.
	var prefix := _info_hash.substr(0, 10) if godot_id == 1 else _random_prefix()
	return prefix + str(godot_id).pad_zeros(10)


# The host's derived peer_id, recomputed by a client from the room hash alone.
func _host_peer_id() -> String:
	return _info_hash.substr(0, 10) + str(1).pad_zeros(10)


func _random_prefix() -> String:
	var chars := "0123456789abcdef"
	var prefix := ""
	for i in 10:
		prefix += chars[randi() % chars.length()]
	return prefix
