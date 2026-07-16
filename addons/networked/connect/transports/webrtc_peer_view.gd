## The state home for a [WebRTCTransport] connection.
##
## A WebRTC connection carries the largest, longest-lived runtime state of any
## transport: a [WebRTCSession] and a [WebRTCSignaler] whose offer, answer, and
## ICE exchange runs for the whole session, not just the connect window, because
## every late joiner is a fresh exchange the host must answer. This view owns
## both, pumps them from [method poll], and lives exactly as long as its peer is
## assigned. The stateless [WebRTCTransport] builds it and hands it the signaler.
## [codeblock]
## ┌───────────────┐ open_client/open_host ┌───────────────┐
## │ WebRTCTransport│ ────────────────────▶ │ WebRTCPeerView │  poll(dt) pumps
## │  (config only) │   returns webrtc_peer │ session+signaler│  the exchange
## └───────────────┘                        └───────────────┘
## [/codeblock]
class_name WebRTCPeerView
extends NetwPeerView

var _transport: WebRTCTransport
var _signaler: WebRTCSignaler
var _session: WebRTCSession
var _is_server := false
var _signaling_ready := false
var _connect_started_ms := 0
var _connect_offer_progress_sent := false
var _step: StringName = &""
var _message: String = ""

# The attempt establishing this connection, cleared once it resolves. WebRTC
# failures that never reach the peer's own connection status resolve it directly.
var _attempt_ref: WeakRef


func _init(
		transport: WebRTCTransport,
		signaler: WebRTCSignaler,
		attempt: NetwConnectAttempt = null,
) -> void:
	_transport = transport
	_signaler = signaler
	_attempt_ref = weakref(attempt) if attempt else null


## Builds the session, opens a server room, and returns its [MultiplayerPeer].
func open_host(options: LobbyDirectory.HostOptions) -> MultiplayerPeer:
	_is_server = true
	_build_session()
	if _session.create_server() != OK:
		_teardown()
		return null
	var err := _signaler._open("", 1)
	if err != OK:
		Netw.dbg.error("WebRTC signaler open failed: %s", [error_string(err)])
		_teardown()
		return null
	var room := _signaler._room_id()
	Netw.dbg.info("Room session ready at `%s` (saved to clipboard).", [room])
	# TODO: move the clipboard copy and local-room registration up to the host
	# popup on join_address() becoming available.
	DisplayServer.clipboard_set(room)
	WebRTCTransport.register_local_room(room)
	_adopt_peer(_peer_from_session())
	return _peer


## Builds the session, opens an offer to [param room_id], and returns its peer.
func open_client(room_id: String) -> MultiplayerPeer:
	_is_server = false
	_build_session()
	if WebRTCTransport.is_local_room(room_id):
		_session.is_local_session = true
	var client_id := randi() % 1000000 + 2
	if _session.create_client(client_id) != OK:
		_teardown()
		return null
	_connect_started_ms = Time.get_ticks_msec()
	var err := _signaler._open(room_id, client_id)
	if err != OK:
		Netw.dbg.error("WebRTC signaler open failed: %s", [error_string(err)])
		_teardown()
		return null
	_report(&"discovery", "Reaching signaling...", 0.55)
	_adopt_peer(_peer_from_session())
	return _peer


func display_name() -> String:
	return "WebRTC"


func join_address() -> String:
	if _signaler and not _signaler._room_id().is_empty():
		return _signaler._room_id()
	return ""


func diagnostics(peer_id: int) -> Dictionary:
	return _session.connection_diagnostics(peer_id) if _session else { }


func describe_progress() -> Dictionary:
	return { "step": _step, "message": _message }


func poll(_dt: float) -> void:
	if _session:
		_session.poll(_dt)
	if _signaler:
		_signaler._poll(_dt)
	_poll_signaling_check()


func close() -> void:
	if _signaler and not _signaler._room_id().is_empty():
		WebRTCTransport.unregister_local_room(_signaler._room_id())
	_teardown()
	_is_server = false
	_signaling_ready = false
	_connect_started_ms = 0
	super()


# Constructs and wires the session and signaler from the transport's config.
func _build_session() -> void:
	_session = WebRTCSession.new()
	var raw_servers := _transport.effective_ice_servers()
	_session.ice_servers = (
			WebRTCTransport.filter_ice_servers(raw_servers)
			if _transport.filter_unsupported_turn
			else raw_servers
	)
	_session.connect_retry = _transport.connect_retry
	_session.max_connect_attempts = _transport.max_connect_attempts
	_session.gather_timeout = _transport.gather_timeout
	_session.topup_interval = _transport.topup_interval

	_session.signal_out.connect(_signaler._send)
	_session.signal_out.connect(_on_session_signal_out)
	_signaler.received.connect(_session.deliver)
	_session.native_connected.connect(_on_native_connected)
	_session.native_connected.connect(_signaler._on_session_connected)
	_session.native_disconnected.connect(_on_native_disconnected)
	_session.failed.connect(_on_session_failed)
	_signaler.ready.connect(_on_signaling_connected)
	_signaler.lost.connect(_on_signaling_disconnected)
	_signaler.unreachable.connect(_on_signaling_unreachable)


# Tears down the session and signaler, unwiring every connection.
func _teardown() -> void:
	if _session:
		if _signaler:
			if _session.signal_out.is_connected(_signaler._send):
				_session.signal_out.disconnect(_signaler._send)
			if _session.native_connected.is_connected(_signaler._on_session_connected):
				_session.native_connected.disconnect(_signaler._on_session_connected)
		if _session.signal_out.is_connected(_on_session_signal_out):
			_session.signal_out.disconnect(_on_session_signal_out)
		if _session.native_connected.is_connected(_on_native_connected):
			_session.native_connected.disconnect(_on_native_connected)
		if _session.native_disconnected.is_connected(_on_native_disconnected):
			_session.native_disconnected.disconnect(_on_native_disconnected)
		if _session.failed.is_connected(_on_session_failed):
			_session.failed.disconnect(_on_session_failed)
		_session.close()
	if _signaler:
		if _session and _signaler.received.is_connected(_session.deliver):
			_signaler.received.disconnect(_session.deliver)
		if _signaler.ready.is_connected(_on_signaling_connected):
			_signaler.ready.disconnect(_on_signaling_connected)
		if _signaler.lost.is_connected(_on_signaling_disconnected):
			_signaler.lost.disconnect(_on_signaling_disconnected)
		if _signaler.unreachable.is_connected(_on_signaling_unreachable):
			_signaler.unreachable.disconnect(_on_signaling_unreachable)
		_signaler._close()
	_session = null
	_signaler = null
	_connect_offer_progress_sent = false


func _peer_from_session() -> MultiplayerPeer:
	return _session.webrtc_peer if _session else null


func _on_native_connected(_id: int) -> void:
	Netw.dbg.info("WebRTC native connection established.")


func _on_native_disconnected(_id: int) -> void:
	Netw.dbg.info("WebRTC native connection lost.")


func _on_session_failed(id: int, reason: String) -> void:
	_connect_started_ms = 0
	var diags := _session.connection_diagnostics(id)
	var status := NetwConnectResult.Status.UNREACHABLE
	if reason == "HOST_UNRESPONSIVE":
		status = NetwConnectResult.Status.TIMED_OUT
	var result := NetwConnectResult.unreachable(StringName(reason), "", diags)
	result.status = status
	_fail(result)


func _on_signaling_connected() -> void:
	_signaling_ready = true
	_report(&"handshake", "Exchanging connection info...", 0.65)


func _on_signaling_disconnected() -> void:
	_signaling_ready = false
	if not _is_server and _session and not _session._connected_ids.has(1):
		_connect_started_ms = 0
		_fail(NetwConnectResult.unreachable(
			&"SIGNALING_UNAVAILABLE", "Could not reach signaling.",
		))


func _on_signaling_unreachable() -> void:
	_signaling_ready = false
	if not _is_server and _session and not _session._connected_ids.has(1):
		_connect_started_ms = 0
		_fail(NetwConnectResult.unreachable(
			&"SIGNALING_UNREACHABLE", "Could not reach any signaling server.",
		))


func _on_session_signal_out(
		_to_multiplayer_id: int,
		_to_signaler_id: String,
		kind: String,
		_payload: Dictionary,
) -> void:
	if kind != "offer" or _connect_offer_progress_sent:
		return
	_connect_offer_progress_sent = true
	_report(&"traversal", "Negotiating peer link...", 0.8)


# Fails a stalled join once the connect budget elapses without signaling.
func _poll_signaling_check() -> void:
	if _is_server or _session == null or _connect_started_ms <= 0:
		return
	if _session._connected_ids.has(1):
		return
	var elapsed := (Time.get_ticks_msec() - _connect_started_ms) / 1000.0
	var threshold := _transport._timeout_hint(null) - 0.1
	if elapsed >= threshold and not _signaling_ready:
		_connect_started_ms = 0
		_fail(NetwConnectResult.unreachable(
			&"SIGNALING_UNAVAILABLE", "Could not reach signaling.",
		))


# Records progress and forwards it to the establishing attempt.
func _report(step: StringName, message: String, ratio: float) -> void:
	_step = step
	_message = message
	var attempt := _attempt_ref.get_ref() as NetwConnectAttempt if _attempt_ref else null
	if attempt:
		attempt.report(step, message, ratio)


# Resolves the establishing attempt with a WebRTC-specific failure.
func _fail(result: NetwConnectResult) -> void:
	var attempt := _attempt_ref.get_ref() as NetwConnectAttempt if _attempt_ref else null
	if attempt and not attempt.is_done():
		attempt.resolve(result)


# Adopts the assigned peer once the session produces it.
func _adopt_peer(peer: MultiplayerPeer) -> void:
	_peer = peer
