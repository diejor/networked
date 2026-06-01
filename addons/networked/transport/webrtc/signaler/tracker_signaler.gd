## [WebRTCSignaler] over WebTorrent compatible trackers, no signaling server.
##
## A tracker is a content addressed rendezvous keyed by a 20 char
## [code]info_hash[/code]. This signaler owns that addressing entirely. It
## embeds the engine [code]multiplayer_id[/code] into the 20 char WebTorrent
## [code]peer_id[/code], extracts it back on receipt, and tunnels ICE through
## the offer and answer slots, so a [WebRTCSession] never sees a tracker id.
## [br][br]
## A tracker only routes one offer and one matching answer per rendezvous, so
## per-candidate trickle has no slot to ride. This signaler instead bundles the
## candidates gathered within [member gather_grace] into the one offer and one
## answer the tracker forwards, then delivers any straggler as a
## [code]type:"candidate"[/code] message in a fresh offer slot. Inbound, it
## unbundles each carried candidate into its own [signal WebRTCSignaler.received]
## emit, so the session stays pure trickle and never learns about bundling.
## [codeblock]
## TrackerSignaler.new(trackers)
## open("", 1)                     # host: generates info_hash
## open(room_hash, client_id)      # client: normalizes the hash
##
## peer_id = rand10 + multiplayer_id.pad_zeros(10)
## multiplayer_id = peer_id.substr(10, 10).to_int()
##
## client offer  -> offers[].offer = { sdp, candidates:[...] }
## host answer   -> answer        = { sdp, candidates:[...] }
## [/codeblock]
class_name TrackerSignaler
extends WebRTCSignaler

## Seconds to keep tracker signaling alive after the native WebRTC link is up.
const SIGNALING_CLOSE_DELAY := 3.0

var trackers: Array[String] = []

## Seconds the offer and answer wait while ICE candidates gather, so they can be
## bundled into the one offer and one answer a tracker forwards. Candidates that
## arrive after the bundle is sent are trickled in their own offer slot.
var gather_grace: float = 0.4

## Skips [member gather_grace] and trickles every candidate in its own offer
## slot, for trackers that tolerate the extra announces. The default bundles
## first because most trackers rate limit announces.
var allow_tracker_trickle_ice: bool = false

var _tracker: WebTorrentTrackerClient = null
var _is_server := false
var _info_hash := ""
var _local_peer_id := ""
var _local_godot_id := 0
var _server_wt_id := ""
var _native_up := false

# Client offer state.
var _client_offer_sdp := ""
var _client_offer_id := ""
var _client_candidates: Array[Dictionary] = []
var _offer_pending := false
var _offer_announced := false
var _offer_grace := 0.0

# to_signaler_id -> { sdp, offer_id, candidates, grace, sent } pending answer.
var _answers: Dictionary = {}

var _peer_map := {}
var _handled_offers := {}
var _handled_answers := {}
var _announce_timer := 0.0
var _signaling_close_delay := -1.0


func _init(p_trackers: Array[String] = []) -> void:
	trackers = p_trackers


func open(p_room_id: String, local_multiplayer_id: int) -> Error:
	_local_godot_id = local_multiplayer_id
	_is_server = local_multiplayer_id == 1
	_local_peer_id = _generate_peer_id(local_multiplayer_id)
	if _is_server:
		# The signaler owns room-id generation; room semantics are its own.
		_info_hash = _generate_hash()
	elif p_room_id.length() != 20:
		_info_hash = p_room_id.sha1_text().substr(0, 20)
	else:
		_info_hash = p_room_id
	Netw.dbg.debug(
		"TrackerSignaler: opening room %s as id %d (peer_id %s...).",
		[_info_hash, local_multiplayer_id, _local_peer_id.substr(0, 6)]
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
	if _is_server:
		_drain_answers(dt)
	else:
		_drive_client_offer(dt)
	_process_signaling_close_delay(dt)


func close() -> void:
	if _tracker:
		_tracker.close()
	_tracker = null


func on_session_connected(multiplayer_id: int) -> void:
	# Wind down signaling once the native link to the host is up.
	if not _is_server and multiplayer_id == 1:
		Netw.dbg.trace("TrackerSignaler: native link up, delaying close.")
		_native_up = true
		_signaling_close_delay = SIGNALING_CLOSE_DELAY


# Buffers an outbound session signal so ICE rides the offer/answer the tracker
# forwards. SDP arms a grace window; candidates bundle into it or trickle once
# the bundle is gone.
func send(
	_to_multiplayer_id: int,
	to_signaler_id: String,
	kind: String,
	payload: Dictionary,
) -> void:
	match kind:
		"offer":
			# A fresh offer (first attempt or a client retry) starts a new bundle.
			_client_offer_sdp = String(payload.get("sdp", ""))
			_client_offer_id = ""
			_client_candidates.clear()
			_offer_pending = true
			_offer_announced = false
			_offer_grace = 0.0 if allow_tracker_trickle_ice else gather_grace
		"answer":
			var answer := _ensure_answer(to_signaler_id)
			answer["sdp"] = String(payload.get("sdp", ""))
			answer["offer_id"] = String(_peer_map.get(to_signaler_id + "_offer_id", ""))
			answer["candidates"] = []
			answer["grace"] = 0.0 if allow_tracker_trickle_ice else gather_grace
			answer["sent"] = false
		"candidate":
			if _is_server:
				_host_candidate(to_signaler_id, payload)
			else:
				_client_candidate(payload)


# -- Client offer -----------------------------------------------------------

# Announces the bundled offer once its grace elapses, then re-announces it until
# the native link is up so a host that joins later still receives it.
func _drive_client_offer(dt: float) -> void:
	if _offer_pending and not _offer_announced:
		_offer_grace -= dt
		if _offer_grace <= 0.0 and _tracker.has_open():
			_offer_announced = true
			_announce_timer = 0.0
			Netw.dbg.trace(
				"TrackerSignaler: announcing bundled offer (%d candidate(s)).",
				[_client_candidates.size()]
			)
			_tracker.broadcast(_build_announce())
		return
	if _offer_announced and not _native_up and _announce_timer > 2.0:
		_announce_timer = 0.0
		Netw.dbg.trace("TrackerSignaler: re-announcing offer to reach host.")
		_tracker.broadcast(_build_announce())


func _client_candidate(payload: Dictionary) -> void:
	if _offer_announced:
		# Straggler past the bundle: ride a fresh offer slot as a candidate.
		_trickle_to(_server_wt_id, payload)
	else:
		_client_candidates.append(payload)


# -- Host answer ------------------------------------------------------------

# Sends each pending answer once its grace elapses, bundling the candidates
# gathered in the meantime, reusing the offer_id the tracker still holds.
func _drain_answers(dt: float) -> void:
	for to_peer: String in _answers:
		var answer: Dictionary = _answers[to_peer]
		if answer["sent"] or String(answer["sdp"]).is_empty():
			continue
		answer["grace"] -= dt
		if answer["grace"] > 0.0:
			continue
		answer["sent"] = true
		Netw.dbg.trace(
			"TrackerSignaler: sending bundled [ANSWER] to %s... (%d candidate(s)).",
			[to_peer.substr(0, 6), (answer["candidates"] as Array).size()]
		)
		_broadcast(_build_answer(to_peer, answer))


func _host_candidate(to_peer: String, payload: Dictionary) -> void:
	var answer: Variant = _answers.get(to_peer)
	if answer != null and answer["sent"]:
		# Straggler past the bundled answer: reach the client in a fresh slot.
		_trickle_to(to_peer, payload)
		return
	(_ensure_answer(to_peer)["candidates"] as Array).append(payload)


func _ensure_answer(to_peer: String) -> Dictionary:
	if not _answers.has(to_peer):
		_answers[to_peer] = {
			"sdp": "",
			"offer_id": "",
			"candidates": [],
			"grace": 0.0 if allow_tracker_trickle_ice else gather_grace,
			"sent": false,
		}
	return _answers[to_peer]


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


# Sends the first announce when a tracker socket opens.
func _announce_to(ws: WebSocketPeer) -> void:
	_tracker.send(ws, _build_announce())


# Builds the announce payload, bundling the buffered candidates into the offer
# once the gather grace has elapsed.
func _build_announce() -> Dictionary:
	var offers := []
	if not _is_server and _offer_announced and not _client_offer_sdp.is_empty():
		if _client_offer_id.is_empty():
			_client_offer_id = _generate_hash()
		offers.append({
			"offer_id": _client_offer_id,
			"offer": {
				"type": "offer",
				"sdp": _client_offer_sdp,
				"candidates": _client_candidates.duplicate(),
			},
		})
	return {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"numwant": 50,
		"offers": offers,
	}


func _build_answer(to_peer: String, answer: Dictionary) -> Dictionary:
	var msg := _directed_announce(to_peer)
	msg["answer"] = {
		"type": "answer",
		"sdp": answer["sdp"],
		"candidates": (answer["candidates"] as Array).duplicate(),
	}
	if not String(answer["offer_id"]).is_empty():
		msg["offer_id"] = answer["offer_id"]
	return msg


# Sends a single candidate to to_peer in its own offer slot, marked so the
# receiver routes it as ICE instead of treating it as a new offer.
func _trickle_to(to_peer: String, payload: Dictionary) -> void:
	var msg := {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"numwant": 1,
		"offers": [{ "offer_id": _generate_hash(), "offer": payload }],
	}
	if not to_peer.is_empty():
		msg["to_peer_id"] = to_peer
	Netw.dbg.trace("TrackerSignaler: trickling tunneled [CANDIDATE].")
	_broadcast(msg)


# Decodes a tracker packet and reports it up as a received() signal, dropping
# tracker replays.
func _parse_packet(data: Dictionary) -> void:
	if data.get("info_hash", "") != _info_hash:
		return

	var remote_peer_id: String = data.get("peer_id", "")
	if remote_peer_id == _local_peer_id or remote_peer_id.length() != 20:
		return

	var godot_id: int = remote_peer_id.substr(10, 10).to_int()

	if not _is_server and _server_wt_id.is_empty():
		_server_wt_id = remote_peer_id
		Netw.dbg.debug(
			"TrackerSignaler: client found server peer_id %s...",
			[_server_wt_id.substr(0, 6)]
		)

	if data.has("offer"):
		_handle_inbound(data, godot_id, remote_peer_id, "offer", _handled_offers)
	elif data.has("answer"):
		_handle_inbound(data, godot_id, remote_peer_id, "answer", _handled_answers)


# Routes one inbound offer/answer slot: a candidate marker fans straight out,
# while real SDP reports once (deduped) then unbundles its carried candidates.
func _handle_inbound(
	data: Dictionary, godot_id: int, remote_peer_id: String,
	slot: String, handled: Dictionary,
) -> void:
	var payload: Variant = data.get(slot)
	if typeof(payload) != TYPE_DICTIONARY:
		return
	if (payload as Dictionary).get("type") == "candidate":
		received.emit(godot_id, remote_peer_id, "candidate", payload)
		return

	var signal_id := String(data.get("offer_id", ""))
	if _has_handled_signal(handled, remote_peer_id, signal_id, payload):
		Netw.dbg.trace(
			"TrackerSignaler: ignoring duplicate [%s] from id %d.",
			[slot.to_upper(), godot_id]
		)
		return
	if slot == "offer":
		_peer_map[remote_peer_id + "_offer_id"] = signal_id
	_mark_handled_signal(handled, remote_peer_id, signal_id, payload)
	Netw.dbg.debug("TrackerSignaler: [%s] from id %d.", [slot.to_upper(), godot_id])
	received.emit(godot_id, remote_peer_id, slot, payload)
	_emit_bundled_candidates(godot_id, remote_peer_id, payload)


# Fans each candidate carried inside an offer/answer out as its own signal, so
# the session applies them as ordinary trickle ICE.
func _emit_bundled_candidates(
	godot_id: int, remote_peer_id: String, payload: Dictionary
) -> void:
	var candidates: Variant = payload.get("candidates", [])
	if typeof(candidates) != TYPE_ARRAY:
		return
	for candidate: Variant in candidates:
		if typeof(candidate) == TYPE_DICTIONARY:
			received.emit(godot_id, remote_peer_id, "candidate", candidate)


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
		_tracker.close()
	_tracker = null


func _directed_announce(to_peer: String) -> Dictionary:
	return {
		"action": "announce",
		"info_hash": _info_hash,
		"peer_id": _local_peer_id,
		"to_peer_id": to_peer,
	}


func _broadcast(data: Dictionary) -> void:
	if _tracker:
		_tracker.broadcast(data)


# -- Signal dedup -----------------------------------------------------------

func _has_handled_signal(
	handled: Dictionary, remote_peer_id: String, signal_id: String,
	payload: Dictionary,
) -> bool:
	return handled.has(_signal_key(remote_peer_id, signal_id, payload))


func _mark_handled_signal(
	handled: Dictionary, remote_peer_id: String, signal_id: String,
	payload: Dictionary,
) -> void:
	handled[_signal_key(remote_peer_id, signal_id, payload)] = true


func _signal_key(
	remote_peer_id: String, signal_id: String, payload: Dictionary,
) -> String:
	if not signal_id.is_empty():
		return remote_peer_id + ":" + signal_id
	return remote_peer_id + ":" + String(payload.get("sdp", "")).sha1_text()


# -- Id generation ----------------------------------------------------------

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
