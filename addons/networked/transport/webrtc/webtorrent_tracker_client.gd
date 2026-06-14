## Shared WebTorrent compatible tracker WebSocket transport.
##
## A tracker connection is a swarm rendezvous keyed by a 20 char
## [code]info_hash[/code]. This client owns the [WebSocketPeer] set, drives
## connect and close detection, and surfaces every decoded message so an owner
## can run its own announce protocol on top. [TrackerSignaler] uses it to relay
## SDP offers and answers. [WebTorrentDirectory] uses it to gossip room cards on
## a board hash.
## [codeblock]
## var tracker := WebTorrentTrackerClient.new()
## tracker.message_received.connect(_on_message)
## tracker.socket_opened.connect(_announce_to)   # send first announce
## tracker.connect_to(trackers)
##
## func _process(_dt):
##     tracker.poll()                            # drain packets each frame
##
## tracker.broadcast(announce_payload)           # to all open sockets
## [/codeblock]
class_name WebTorrentTrackerClient
extends RefCounted

## Emitted when the first tracker socket reaches the open state.
signal connected
## Emitted once every previously open tracker socket has closed.
signal disconnected
## Emitted once when every tracker socket closes before any socket opens.
signal unreachable
## Emitted when [param ws] first opens, so the owner can send its first
## announce to that socket.
signal socket_opened(ws: WebSocketPeer)
## Emitted for each decoded tracker message. Tracker level [code]warning[/code]
## and [code]failure reason[/code] payloads are logged as debug chatter and
## withheld.
signal message_received(data: Dictionary)

## How long a socket may sit in the connecting state before it is dropped.
const CONNECT_TIMEOUT_USEC := 10_000_000

static var _shared_clients := { }

var _sockets: Array[WebSocketPeer] = []
var _any_open := false
var _signaled_unreachable := false
var _last_poll_frame := -1


## Acquires a shared client for [param urls].
##
## The returned [Dictionary] contains [code]client[/code] and [code]error[/code].
## First acquire opens sockets. Later acquires reuse them and increment a
## reference count. Call [method release_shared] with the same [param urls].
static func acquire_shared(urls: Array[String]) -> Dictionary:
	var key := _shared_key(urls)
	if _shared_clients.has(key):
		var existing: Dictionary = _shared_clients[key]
		var existing_client := existing.client as WebTorrentTrackerClient
		if existing_client.is_active():
			existing.refs = int(existing.refs) + 1
			return { "client": existing_client, "error": OK }
		existing_client.close()
		_shared_clients.erase(key)

	var client := WebTorrentTrackerClient.new()
	var err := client.connect_to(urls)
	if err != OK:
		return { "client": null, "error": err }
	_shared_clients[key] = { "client": client, "refs": 1 }
	return { "client": client, "error": OK }


## Releases a client acquired with [method acquire_shared].
##
## The final release closes the shared sockets and removes the registry entry.
static func release_shared(urls: Array[String], client: WebTorrentTrackerClient) -> void:
	if client == null:
		return
	var key := _shared_key(urls)
	if not _shared_clients.has(key):
		return
	var existing: Dictionary = _shared_clients[key]
	if existing.client != client:
		return
	existing.refs = int(existing.refs) - 1
	if int(existing.refs) > 0:
		return
	client.close()
	_shared_clients.erase(key)


## Clears all shared tracker clients and closes their sockets.
##
## This is primarily used for testing teardown to prevent static leaks.
static func clear_shared_clients() -> void:
	for key in _shared_clients:
		var entry: Dictionary = _shared_clients[key]
		var client := entry.client as WebTorrentTrackerClient
		if client:
			client.close()
	_shared_clients.clear()


# Builds a stable registry key from tracker URLs.
static func _shared_key(urls: Array[String]) -> String:
	var sorted := urls.duplicate()
	sorted.sort()
	return JSON.stringify(sorted)


## Opens a [WebSocketPeer] to each url in [param urls], replacing any existing
## connections.
##
## Returns [constant @GlobalScope.OK] when at least one socket starts
## connecting, or [constant @GlobalScope.ERR_CANT_CONNECT] otherwise.
func connect_to(urls: Array[String]) -> Error:
	close()
	_signaled_unreachable = false
	var now := Time.get_ticks_usec()
	for url in urls:
		Netw.dbg.trace("WebTorrentTrackerClient: connecting to %s", [url])
		var ws := WebSocketPeer.new()
		if ws.connect_to_url(url) == OK:
			ws.set_meta("url", url)
			ws.set_meta("connect_time", now)
			_sockets.append(ws)
		else:
			Netw.dbg.info(
				"WebTorrentTrackerClient: failed to connect %s",
				[url],
			)
	Netw.dbg.debug(
		"WebTorrentTrackerClient: %d/%d tracker socket(s) opening.",
		[_sockets.size(), urls.size()],
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
	var frame := Engine.get_process_frames()
	if _last_poll_frame == frame:
		return
	_last_poll_frame = frame

	var now := Time.get_ticks_usec()
	var to_remove: Array[WebSocketPeer] = []

	for ws in _sockets:
		ws.poll()
		var state := ws.get_ready_state()
		if state == WebSocketPeer.STATE_CONNECTING:
			var started: int = ws.get_meta("connect_time", 0)
			if started > 0 and now - started > CONNECT_TIMEOUT_USEC:
				_log_dropped(ws, "timed out")
				ws.close()
				to_remove.append(ws)
			continue

		if state == WebSocketPeer.STATE_CLOSED:
			_log_dropped(ws, "closed")
			to_remove.append(ws)
			continue

		if state == WebSocketPeer.STATE_OPEN:
			if not ws.has_meta("opened"):
				ws.set_meta("opened", true)
				Netw.dbg.debug(
					"WebTorrentTrackerClient: tracker open: %s",
					[ws.get_meta("url", "unknown")],
				)
				if not _any_open:
					_any_open = true
					connected.emit()
				socket_opened.emit(ws)
			while ws.get_available_packet_count() > 0:
				_decode(ws.get_packet())

	for ws in to_remove:
		_sockets.erase(ws)

	if _sockets.is_empty() and not _any_open and not _signaled_unreachable:
		_signaled_unreachable = true
		unreachable.emit()

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


## Returns currently open sockets.
##
## Late consumers use this to announce on sockets that opened before they
## acquired a shared client.
func open_sockets() -> Array[WebSocketPeer]:
	var out: Array[WebSocketPeer] = []
	for ws in _sockets:
		if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
			out.append(ws)
	return out


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
	Netw.dbg.trace(
		"WebTorrentTrackerClient: tracker notice %s",
		[packet_text],
	)


func _log_dropped(ws: WebSocketPeer, why: String) -> void:
	Netw.dbg.info(
		"WebTorrentTrackerClient: tracker %s: %s",
		[why, ws.get_meta("url", "unknown")],
	)
