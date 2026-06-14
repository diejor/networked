## [BackendPeer] base for WebRTC rooms, signaling held behind a
## [WebRTCSignaler].
##
## This base owns the [WebRTCSession] and wires it to a signaler a subclass
## supplies through [method make_signaler], so the WebRTC peer machinery is
## reused unchanged across every signaling transport. [TrackerWebRTCBackend] is
## the WebTorrent implementation. [method create_host_peer] emits
## [signal room_created] with the room id. [method create_join_peer] accepts
## that id as its address.
##
## [br][br]
## Swapping the signaling model (WebTorrent, dedicated, or direct) only
## requires returning a different [WebRTCSignaler] from
## [method make_signaler].
##
## Browser hosts are full peer-to-peer hosts. If the browser throttles an
## unfocused tab, signaling can stall until the tab is visible again. Prefer a
## relay or dedicated host when web-hosted rooms must stay reachable while the
## host tab is backgrounded.
## [br][br]
## [b]Credentials and TURN configuration[/b]
## [br]
## Configure the project setting
## [code]networked/webrtc/turn_credentials_url[/code] to point to your secure
## or backend service that issues ephemeral TURN/STUN credentials. During
## connection setup, the backend will dynamically query this URL to populate
## the active [member global_ice_servers].
## [codeblock]
## networked/webrtc/turn_credentials_url = "https://api.mygame.com/turn"
## [/codeblock]
## If your credentials backend requires authentication (such as custom tokens
## or signatures), define the project setting
## [code]networked/webrtc/turn_credentials_headers[/code] with the headers to
## send:
## [codeblock]
## PackedStringArray
##  ┠╴"Authorization: Bearer my_secret_token"
##  ┖╴"X-Api-Key: my_secret_api_key"
## [/codeblock]
@tool
@abstract
class_name WebRTCBackend
extends BackendPeer

## Globally cached ICE servers list. Stored class-wide to propagate dynamically
## to all backend instances.
static var global_ice_servers: Array[Dictionary] = []

## Emitted when the signaler reports a usable signaling route.
signal signaling_connected
## Emitted when the signaler reports its signaling routes gone.
signal signaling_disconnected
## Emitted on the host when the room id is ready to share.
signal room_created(room_id: String)

## Display name advertised by [WebTorrentDirectory] for hosted rooms.
@export var server_name: String = ""

## Optional namespace to isolate signaling and room codes on public networks.
## Non-empty values enable short, player-friendly room codes.
@export var signaling_namespace: String = ""

@export_tool_button("Generate signaling namespace") var _generate_signaling_namespace := func() -> void:
	signaling_namespace = _random_signaling_namespace()

## Character set used to generate short room codes.
## Excludes ambiguous characters (like 0, O, 1, I, l) by default.
@export_multiline var room_code_characters: String = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

## ICE server definitions passed to each [WebRTCPeerConnection].
##
## Each entry is one STUN or TURN server. A STUN entry needs only
## [code]urls[/code]. A TURN relay also needs [code]username[/code] and
## [code]credential[/code].
## [codeblock]
## Array[Dictionary]
##  ┖╴{ }                                  # one entry per ICE server
##     ┠╴urls (String or Array[String])    # {stun,turn}:host:port
##     ┠╴username (String, TURN only)
##     ┖╴credential (String, TURN only)
## [/codeblock]
@export var ice_servers: Array[Dictionary] = [
	{ "urls": ["stun:stun.l.google.com:19302"] },
	{
		"urls": ["turn:openrelay.metered.ca:80"],
		"username": "openrelayproject",
		"credential": "openrelayproject",
	},
	{
		"urls": ["turns:openrelay.metered.ca:443?transport=tcp"],
		"username": "openrelayproject",
		"credential": "openrelayproject",
	},
	{
		"urls": ["turns:openrelay.metered.ca:443"],
		"username": "openrelayproject",
		"credential": "openrelayproject",
	},
]

## Seconds a joining client waits for the native link after sending its offer
## bundle before re-sending it, forwarded to [member WebRTCSession.connect_retry].
@export_range(0.5, 30.0, 0.1, "suffix:s") var connect_retry: float = 8.0

## Offer attempts a joining client makes before it leaves failure to the connect
## budget, forwarded to [member WebRTCSession.max_connect_attempts].
@export_range(1, 10) var max_connect_attempts: int = 3

## Seconds the session waits for ICE gathering to complete before sending the
## final offer/answer top-up, forwarded to [member WebRTCSession.gather_timeout].
@export_range(0.5, 15.0, 0.1, "suffix:s") var gather_timeout: float = 6.0

## Minimum seconds between candidate top-up bundles while ICE is gathering,
## forwarded to [member WebRTCSession.topup_interval].
@export_range(0.05, 5.0, 0.05, "suffix:s") var topup_interval: float = 0.25

## If [code]true[/code], filters out TURN over TCP and TLS configurations on native
## platforms (non-web) to prevent console warnings from libjuice.
@export var filter_unsupported_turn: bool = true

var _session: WebRTCSession = null
var _signaler: WebRTCSignaler = null
var _is_server := false
var _signaling_ready := false
var _connect_started_ms := 0
var _connect_offer_progress_sent := false


## Builds the [WebRTCSignaler] this backend signals through.
@abstract
func make_signaler() -> WebRTCSignaler


## Prepares WebRTC backend by fetching secure credentials when configured.
##
## If the global setting [code]networked/webrtc/turn_credentials_url[/code]
## is set in ProjectSettings, this method performs an asynchronous [HTTPRequest]
## to fetch ICE credentials and populates [member global_ice_servers].
## [br][br]
## Custom headers specified in
## [code]networked/webrtc/turn_credentials_headers[/code] are included in the
## request. If this setting is empty, a standard request with no custom
## headers is sent to preserve compatibility with third-party APIs.
## [br][br]
## The fetched JSON response must match this schema:
## [codeblock]
## Array
##  ┖╴{ }
##     ┠╴urls (String or Array)
##     ┠╴username (String)
##     ┖╴credential (String)
## [/codeblock]
func setup(tree: MultiplayerTree) -> Error:
	if Netw.is_test_env():
		return OK

	if not global_ice_servers.is_empty():
		return OK

	var url := ""
	if ProjectSettings.has_setting("networked/webrtc/turn_credentials_url"):
		url = ProjectSettings.get_setting(
			"networked/webrtc/turn_credentials_url",
		)

	if url.is_empty():
		return OK

	var headers: PackedStringArray = []
	if ProjectSettings.has_setting(
		"networked/webrtc/turn_credentials_headers",
	):
		headers = ProjectSettings.get_setting(
			"networked/webrtc/turn_credentials_headers",
		)

	var http := HTTPRequest.new()
	http.timeout = 5.0
	tree.add_child(http)

	var err := http.request(url, headers)
	if err != OK:
		Netw.dbg.warn(
			"WebRTC credentials request initiation failed: %s",
			[error_string(err)],
		)
		http.queue_free()
		return OK

	var results: Array = await http.request_completed
	http.queue_free()

	var status_code: int = results[1]
	var response_body: PackedByteArray = results[3]

	if status_code == 200:
		var json_text := response_body.get_string_from_utf8()
		var parsed: Variant = JSON.parse_string(json_text)
		if typeof(parsed) == TYPE_ARRAY:
			var servers: Array[Dictionary] = []
			servers.assign(parsed)
			global_ice_servers = servers
			Netw.dbg.info(
				"WebRTC credentials fetched successfully from %s.",
				[url],
			)
		else:
			Netw.dbg.warn(
				"WebRTC credentials parse failed. Response was "
				+ "not a JSON array. Falling back to default ICE servers.",
				[],
			)
	else:
		Netw.dbg.warn(
			"WebRTC credentials fetch failed. Server responded "
			+ "with code %d. Falling back to default ICE servers.",
			[status_code],
		)

	return OK


## Implements [method BackendPeer.create_host_peer] for a WebRTC room.
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("WebRTCBackend: create_host_peer called.")
	_is_server = true
	_build_session_and_signaler()

	if _session.create_server() != OK:
		_clear_session_and_signaler()
		return null

	var err := _signaler.open("", 1)
	if err != OK:
		Netw.dbg.error("WebRTC signaler open failed: %s", [error_string(err)])
		_clear_session_and_signaler()
		return null

	var room := _signaler.room_id()
	room_created.emit(room)
	Netw.dbg.info("Room session ready at `%s` (saved to clipboard).", [room])
	DisplayServer.clipboard_set(room)
	_register_local_room(room)
	return _session.webrtc_peer


## Implements [method BackendPeer.create_join_peer] for a WebRTC room id.
func create_join_peer(
		_tree: MultiplayerTree,
		server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	Netw.dbg.trace("WebRTCBackend: create_join_peer at %s", [server_address])
	_is_server = false
	_build_session_and_signaler()
	if _is_local_room(server_address):
		_session.is_local_session = true

	var client_id := randi() % 1000000 + 2
	# create_client opens the offer toward the server; it is announced once the
	# signaler has an open route.
	if _session.create_client(client_id) != OK:
		_clear_session_and_signaler()
		return null

	_connect_started_ms = Time.get_ticks_msec()

	var err := _signaler.open(server_address, client_id)
	if err != OK:
		Netw.dbg.error("WebRTC signaler open failed: %s", [error_string(err)])
		_clear_session_and_signaler()
		return null
	_set_connect_step(&"discovery")
	_set_connect_message("Reaching signaling...")
	return _session.webrtc_peer


## Implements [method BackendPeer.poll] for session and signaler state.
func poll(dt: float) -> void:
	super.poll(dt)
	if _session:
		_session.poll(dt)
	if _signaler:
		_signaler.poll(dt)
	_poll_signaling_check()


## Starts closing active [WebRTCDataChannel]s before peer teardown.
##
## Callers that can yield should poll or await a few frames after this method
## before freeing the tree or calling [method peer_reset_state].
func close_channels() -> void:
	if _session:
		_session.close_channels()


func _build_session_and_signaler() -> void:
	_session = WebRTCSession.new()
	var raw_servers = (
			global_ice_servers
			if not global_ice_servers.is_empty()
			else ice_servers
	)
	if filter_unsupported_turn:
		_session.ice_servers = _filter_ice_servers(raw_servers)
	else:
		_session.ice_servers = raw_servers
	_session.connect_retry = connect_retry
	_session.max_connect_attempts = max_connect_attempts
	_session.gather_timeout = gather_timeout
	_session.topup_interval = topup_interval
	_signaler = make_signaler()

	_session.signal_out.connect(_signaler.send)
	_session.signal_out.connect(_on_session_signal_out)
	_signaler.received.connect(_session.deliver)
	_session.native_connected.connect(_on_native_connected)
	_session.native_connected.connect(_signaler.on_session_connected)
	_session.native_disconnected.connect(_on_native_disconnected)
	_session.failed.connect(_on_session_failed)
	_signaler.ready.connect(_on_signaling_connected)
	_signaler.lost.connect(_on_signaling_disconnected)
	_signaler.unreachable.connect(_on_signaling_unreachable)


func _clear_session_and_signaler() -> void:
	if _session:
		if _signaler:
			if _session.signal_out.is_connected(_signaler.send):
				_session.signal_out.disconnect(_signaler.send)
			if _session.native_connected.is_connected(_signaler.on_session_connected):
				_session.native_connected.disconnect(_signaler.on_session_connected)
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
		if _session:
			if _signaler.received.is_connected(_session.deliver):
				_signaler.received.disconnect(_session.deliver)
		if _signaler.ready.is_connected(_on_signaling_connected):
			_signaler.ready.disconnect(_on_signaling_connected)
		if _signaler.lost.is_connected(_on_signaling_disconnected):
			_signaler.lost.disconnect(_on_signaling_disconnected)
		if _signaler.unreachable.is_connected(_on_signaling_unreachable):
			_signaler.unreachable.disconnect(_on_signaling_unreachable)
		_signaler.close()

	_session = null
	_signaler = null
	_connect_offer_progress_sent = false


func _on_native_connected(id: int) -> void:
	Netw.dbg.info("WebRTC native connection established with id %d.", [id])
	if not _is_server and id == 1:
		var diags := _session.connection_diagnostics(1)
		var phases: Dictionary = diags.get("phases", { })
		var offer_ms: int = phases.get("offer_ms", 0)
		var answer_ms: int = phases.get("answer_ms", 0)
		var native_ms: int = phases.get("native_ms", 0)
		var offer_t := (
				float(offer_ms - _connect_started_ms) / 1000.0
				if offer_ms > 0 else 0.0
		)
		var answer_t := (
				float(answer_ms - _connect_started_ms) / 1000.0
				if answer_ms > 0 else 0.0
		)
		var native_t := (
				float(native_ms - _connect_started_ms) / 1000.0
				if native_ms > 0 else 0.0
		)
		var total_t := (
				float(Time.get_ticks_msec() - _connect_started_ms) / 1000.0
		)
		var stats: Dictionary = diags.get("candidates", { })
		var host_count := int(stats.get("host", 0))
		var srflx_count := int(stats.get("srflx", 0))
		var relay_count := int(stats.get("relay", 0))
		var relay_str := "no"
		if relay_count > 0:
			if host_count > 0 or srflx_count > 0:
				relay_str = "maybe (relay candidate gathered)"
			else:
				relay_str = "yes"
		Netw.dbg.info(
			"WebRTC join established in %.1fs: offer=%.1fs "
			+ "answer=%.1fs native=%.1fs relay=%s.",
			[total_t, offer_t, answer_t, native_t, relay_str],
		)


func _on_native_disconnected(id: int) -> void:
	Netw.dbg.info("WebRTC native connection lost with id %d.", [id])


func _on_session_failed(id: int, reason: String) -> void:
	_connect_started_ms = 0
	var diags := _session.connection_diagnostics(id)
	var code := StringName(reason)
	var status := ConnectResult.Status.UNREACHABLE
	if reason == "HOST_UNRESPONSIVE":
		status = ConnectResult.Status.TIMED_OUT

	var result := ConnectResult.unreachable(code, "", diags)
	result.status = status
	connect_failed.emit(result)


func _on_signaling_connected() -> void:
	_signaling_ready = true
	signaling_connected.emit()
	_set_connect_step(&"handshake")
	_set_connect_message("Exchanging connection info...")


func _on_signaling_disconnected() -> void:
	_signaling_ready = false
	signaling_disconnected.emit()
	if not _is_server and _session and not _session._connected_ids.has(1):
		_connect_started_ms = 0
		var res := ConnectResult.unreachable(
			&"SIGNALING_UNAVAILABLE",
			"Could not reach signaling.",
		)
		connect_failed.emit(res)


func _on_signaling_unreachable() -> void:
	_signaling_ready = false
	if not _is_server and _session and not _session._connected_ids.has(1):
		_connect_started_ms = 0
		var res := ConnectResult.unreachable(
			&"SIGNALING_UNREACHABLE",
			"Could not reach any signaling server.",
		)
		connect_failed.emit(res)


func _on_session_signal_out(
		_to_multiplayer_id: int,
		_to_signaler_id: String,
		kind: String,
		_payload: Dictionary,
) -> void:
	if kind != "offer" or _connect_offer_progress_sent:
		return
	_connect_offer_progress_sent = true
	_set_connect_step(&"traversal")
	_set_connect_message("Negotiating peer link...")


func _poll_signaling_check() -> void:
	if not _is_server and _session and _connect_started_ms > 0:
		if not _session._connected_ids.has(1):
			var elapsed := (Time.get_ticks_msec() - _connect_started_ms) / 1000.0
			var threshold := connect_timeout_hint() - 0.1
			if elapsed >= threshold and not _signaling_ready:
				_connect_started_ms = 0 # trigger once
				var res := ConnectResult.unreachable(
					&"SIGNALING_UNAVAILABLE",
					"Could not reach signaling.",
				)
				connect_failed.emit(res)


## Returns the active room id, or the parent default.
func get_join_address() -> String:
	if _signaler and not _signaler.room_id().is_empty():
		return _signaler.room_id()
	return super.get_join_address()


## Returns a [code]"Room ID"[/code] [AddressHint].
func get_address_hint() -> AddressHint:
	var placeholder := (
			"5-char code"
			if not signaling_namespace.is_empty()
			else "20-char hex"
	)
	return AddressHint.make(
		"Room ID",
		placeholder,
		"Room identifier copied from the host (also auto-copied to clipboard "
		+ "on host).",
		false,
		false,
	)
## Keeps [method BackendPeer.query_server_info] unsupported for room ids.


##
## WebRTC discovery uses signaling. An [AuthProbeClient] probe would need a full
## ICE handshake, which is too expensive for browser refresh.
func query_server_info(
		_address: String,
		_timeout: float = 2.0,
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


## Budgets the connect timeout for the retry-aware WebRTC join, covering the
## initial ICE gather plus every re-send window so a stalled attempt re-sends
## rather than failing on the first try.
func connect_timeout_hint() -> float:
	return gather_timeout + connect_retry * float(max_connect_attempts) + 4.0


## Preserves authored WebRTC settings after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	if source is WebRTCBackend:
		var other := source as WebRTCBackend
		server_name = other.server_name
		signaling_namespace = other.signaling_namespace
		room_code_characters = other.room_code_characters
		ice_servers = other.ice_servers.duplicate(true)
		connect_retry = other.connect_retry
		max_connect_attempts = other.max_connect_attempts
		gather_timeout = other.gather_timeout
		topup_interval = other.topup_interval
		filter_unsupported_turn = other.filter_unsupported_turn


## Clears the active session and signaler.
func peer_reset_state() -> void:
	Netw.dbg.trace("WebRTCBackend: resetting peer state.")
	if _signaler and not _signaler.room_id().is_empty():
		_unregister_local_room(_signaler.room_id())
	_clear_session_and_signaler()
	_is_server = false
	_signaling_ready = false
	_connect_started_ms = 0


## Returns a diagnostics snapshot for [param peer_id] from the session.
func get_connection_diagnostics(peer_id: int) -> Dictionary:
	if _session:
		return _session.connection_diagnostics(peer_id)
	return { }


## Returns the display name for this backend.
func get_display_name() -> String:
	return "WebRTC"


# Filters out TURN TCP/TLS servers since libjuice only supports TURN UDP.
static func _filter_ice_servers(
		servers: Array[Dictionary],
) -> Array[Dictionary]:
	# Web browsers support TURN over TCP/TLS natively. Native desktop/mobile
	# WebRTC (libjuice) is UDP-only and will print warnings for TCP/TLS URLs.
	if OS.has_feature("web"):
		return servers

	var filtered: Array[Dictionary] = []
	filtered.assign(
		servers.map(_map_ice_server).filter(_is_ice_server_valid),
	)
	return filtered


# Filters unsupported URLs from a server, returning {} if none remain.
static func _map_ice_server(server: Dictionary) -> Dictionary:
	var urls = server.get("urls") as Array
	assert(urls != null, "ICE server configuration is missing 'urls' key")

	var clean_urls = urls.filter(_is_url_supported_native)
	if clean_urls.is_empty():
		return { }

	var copy := server.duplicate()
	copy["urls"] = clean_urls
	return copy


# Returns true if the processed server dictionary is not empty.
static func _is_ice_server_valid(server: Dictionary) -> bool:
	return not server.is_empty()


# Returns true if a URL is not secure TURN (TLS) or TURN over TCP.
static func _is_url_supported_native(url: String) -> bool:
	var u := url.to_lower().strip_edges()
	return not (
			u.begins_with("turns:")
			or u.contains("transport=tcp")
			or u.contains("transport=tls")
	)


# Registers a room ID as active locally on this machine.
static func _register_local_room(room_id: String) -> void:
	if room_id.is_empty():
		return
	var path := "user://local_webrtc_rooms.txt"
	var rooms: Array[String] = []
	if FileAccess.file_exists(path):
		var file := FileAccess.open(path, FileAccess.READ)
		if file:
			while not file.eof_reached():
				var line := file.get_line().strip_edges()
				if not line.is_empty():
					rooms.append(line)
	if not rooms.has(room_id):
		rooms.append(room_id)
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file:
		for r in rooms:
			file.store_line(r)


# Unregisters a room ID.
static func _unregister_local_room(room_id: String) -> void:
	if room_id.is_empty():
		return
	var path := "user://local_webrtc_rooms.txt"
	if not FileAccess.file_exists(path):
		return
	var rooms: Array[String] = []
	var file := FileAccess.open(path, FileAccess.READ)
	if file:
		while not file.eof_reached():
			var line := file.get_line().strip_edges()
			if not line.is_empty() and line != room_id:
				rooms.append(line)
	if rooms.is_empty():
		DirAccess.remove_absolute(path)
	else:
		var write_file := FileAccess.open(path, FileAccess.WRITE)
		if write_file:
			for r in rooms:
				write_file.store_line(r)


# Returns true if the room ID was hosted locally.
static func _is_local_room(room_id: String) -> bool:
	if room_id.is_empty():
		return false
	var path := "user://local_webrtc_rooms.txt"
	if not FileAccess.file_exists(path):
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file:
		while not file.eof_reached():
			var line := file.get_line().strip_edges()
			if line == room_id:
				return true
	return false


# Builds a fresh random namespace for the editor tool button.
func _random_signaling_namespace() -> String:
	const CHARS := "abcdefghijklmnopqrstuvwxyz0123456789"
	var out := ""
	for i in 15:
		out += CHARS[randi() % CHARS.length()]
	return out
