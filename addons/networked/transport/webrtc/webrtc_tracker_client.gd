## Shared WebTorrent compatible tracker WebSocket transport.
##
## A tracker connection is a swarm rendezvous keyed by a 20 char
## [code]info_hash[/code]. This client owns the [WebSocketPeer] set, drives
## connect and close detection, and surfaces every decoded message so an owner
## can run its own announce protocol on top. [WebRTCBackend] uses it to relay
## SDP offers and answers. [WebRTCDirectory] uses it to gossip room cards on a
## board hash.
## [codeblock]
## var tracker := WebRTCTrackerClient.new()
## tracker.message_received.connect(_on_message)
## tracker.socket_opened.connect(_announce_to)   # send first announce
## tracker.connect_to(trackers)
##
## func _process(_dt):
##     tracker.poll()                            # drain packets each frame
##
## tracker.broadcast(announce_payload)           # to all open sockets
## [/codeblock]
class_name WebRTCTrackerClient
extends RefCounted

## Emitted when the first tracker socket reaches the open state.
signal connected
## Emitted once every previously open tracker socket has closed.
signal disconnected
## Emitted when [param ws] first opens, so the owner can send its first
## announce to that socket.
signal socket_opened(ws: WebSocketPeer)
## Emitted for each decoded tracker message. Tracker level [code]warning[/code]
## and [code]failure reason[/code] payloads are logged as debug chatter and
## withheld.
signal message_received(data: Dictionary)

## How long a socket may sit in the connecting state before it is dropped.
const CONNECT_TIMEOUT_USEC := 10_000_000

var _sockets: Array[WebSocketPeer] = []
var _any_open := false


## Opens a [WebSocketPeer] to each url in [param urls], replacing any existing
## connections.
##
## Returns [constant @GlobalScope.OK] when at least one socket starts
## connecting, or [constant @GlobalScope.ERR_CANT_CONNECT] otherwise.
func connect_to(urls: Array[String]) -> Error:
	close()
	var now := Time.get_ticks_usec()
	for url in urls:
		Netw.dbg.trace("WebRTCTrackerClient: connecting to %s", [url])
		var ws := WebSocketPeer.new()
		if ws.connect_to_url(url) == OK:
			ws.set_meta("url", url)
			ws.set_meta("connect_time", now)
			_sockets.append(ws)
		else:
			Netw.dbg.warn(
				"WebRTCTrackerClient: failed to connect %s", [url],
				func(m): push_warning(m)
			)
	Netw.dbg.debug(
		"WebRTCTrackerClient: %d/%d tracker socket(s) opening.",
		[_sockets.size(), urls.size()]
	)
	if _sockets.is_empty():
		return ERR_CANT_CONNECT
	return OK


## Polls every socket, draining packets into [signal message_received] and
## emitting [signal connected], [signal socket_opened], and
## [signal disconnected] across state transitions.
func poll() -> void:
	if _sockets.is_empty():
		return

	var now := Time.get_ticks_usec()
	var to_remove: Array[WebSocketPeer] = []

	for ws in _sockets:
		ws.poll()
		var state := ws.get_ready_state()
		if state == WebSocketPeer.STATE_CONNECTING:
			var started: int = ws.get_meta("connect_time", 0)
			if started > 0 and now - started > CONNECT_TIMEOUT_USEC:
				_warn_dropped(ws, "timed out")
				ws.close()
				to_remove.append(ws)
			continue

		if state == WebSocketPeer.STATE_CLOSED:
			_warn_dropped(ws, "closed")
			to_remove.append(ws)
			continue

		if state == WebSocketPeer.STATE_OPEN:
			if not ws.has_meta("opened"):
				ws.set_meta("opened", true)
				Netw.dbg.debug(
					"WebRTCTrackerClient: tracker open: %s",
					[ws.get_meta("url", "unknown")]
				)
				if not _any_open:
					_any_open = true
					connected.emit()
				socket_opened.emit(ws)
			while ws.get_available_packet_count() > 0:
				_decode(ws.get_packet())

	for ws in to_remove:
		_sockets.erase(ws)

	if _any_open and not has_open():
		_any_open = false
		disconnected.emit()


## Sends [param data] as JSON to every open socket.
func broadcast(data: Dictionary) -> void:
	for ws in _sockets:
		send(ws, data)


## Sends [param data] as JSON to [param ws] when it is open.
func send(ws: WebSocketPeer, data: Dictionary) -> void:
	if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
		ws.send_text(JSON.stringify(data))


## Returns [code]true[/code] while at least one socket is open.
func has_open() -> bool:
	for ws in _sockets:
		if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
			return true
	return false


## Returns [code]true[/code] while any socket is still open or connecting.
##
## A client whose sockets all timed out or closed reports [code]false[/code],
## which an owner can treat as a cue to reconnect.
func is_active() -> bool:
	return not _sockets.is_empty()


## Closes every socket and clears connection state.
func close() -> void:
	for ws in _sockets:
		ws.close()
	_sockets.clear()
	_any_open = false


func _decode(packet: PackedByteArray) -> void:
	var packet_text := packet.get_string_from_utf8()
	var parsed: Variant = JSON.parse_string(packet_text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return
	var data: Dictionary = parsed
	if data.has("warning") or data.has("failure reason"):
		_log_tracker_notice(packet_text)
		return
	message_received.emit(data)


# Logs tracker protocol notices without raising engine warnings.
func _log_tracker_notice(packet_text: String) -> void:
	Netw.dbg.debug(
		"WebRTCTrackerClient: tracker notice %s",
		[packet_text]
	)


func _warn_dropped(ws: WebSocketPeer, why: String) -> void:
	Netw.dbg.warn(
		"WebRTCTrackerClient: tracker %s: %s",
		[why, ws.get_meta("url", "unknown")],
		func(m): push_warning(m)
	)
