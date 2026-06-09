## [BackendPeer] base for WebRTC rooms, signaling held behind a
## [WebRTCSignaler].
##
## This base owns the [WebRTCSession] and wires it to a signaler a subclass
## supplies through [method _make_signaler], so the WebRTC peer machinery is
## reused unchanged across every signaling transport. [TrackerWebRTCBackend] is
## the WebTorrent implementation. [method create_host_peer] emits
## [signal room_created] with the room id. [method create_join_peer] accepts
## that id as its address.
##
## Browser hosts are full peer-to-peer hosts. If the browser throttles an
## unfocused tab, signaling can stall until the tab is visible again. Prefer a
## relay or dedicated host when web-hosted rooms must stay reachable while the
## host tab is backgrounded.
## [br][br]
## [b]Credentials and TURN configuration[/b]
## [br]
## Configure the project setting [code]networked/webrtc/turn_credentials_url[/code]
## to point to your secure Cloudflare Worker or backend service that issues ephemeral
## TURN/STUN credentials. During connection setup, the backend will dynamically
## query this URL to populate the active [member global_ice_servers].
## [codeblock]
## # session.signal_out -> signaler.send,  signaler.received -> session.deliver
## @abstract func _make_signaler() -> WebRTCSignaler
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

## Seconds a joining client waits for the native link before re-offering to the
## host with a fresh rendezvous, forwarded to
## [member WebRTCSession.connect_retry].
@export_range(0.5, 30.0, 0.1, "suffix:s") var connect_retry: float = 4.0

## Offer attempts a joining client makes before it leaves failure to the connect
## budget, forwarded to [member WebRTCSession.max_connect_attempts].
@export_range(1, 10) var max_connect_attempts: int = 3

var _session: WebRTCSession = null
var _signaler: WebRTCSignaler = null
var _is_server := false


## Builds the [WebRTCSignaler] this backend signals through.
@abstract
func _make_signaler() -> WebRTCSignaler


## Prepares WebRTC backend by fetching secure credentials when configured.
##
## If the global setting [code]networked/webrtc/turn_credentials_url[/code]
## is set in ProjectSettings, this method performs an asynchronous [HTTPRequest]
## to fetch ICE credentials and populates [member global_ice_servers].
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
		url = ProjectSettings.get_setting("networked/webrtc/turn_credentials_url")

	if url.is_empty():
		return OK

	var http := HTTPRequest.new()
	http.timeout = 5.0
	tree.add_child(http)

	var err := http.request(url)
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

	var client_id := randi() % 1000000 + 2
	# create_client opens the offer toward the server; it is announced once the
	# signaler has an open route.
	if _session.create_client(client_id) != OK:
		_clear_session_and_signaler()
		return null

	var err := _signaler.open(server_address, client_id)
	if err != OK:
		Netw.dbg.error("WebRTC signaler open failed: %s", [error_string(err)])
		_clear_session_and_signaler()
		return null
	return _session.webrtc_peer


## Implements [method BackendPeer.poll] for session and signaler state.
func poll(dt: float) -> void:
	if _session:
		_session.poll(dt)
	if _signaler:
		_signaler.poll(dt)


## Starts closing active [WebRTCDataChannel]s before peer teardown.
##
## Callers that can yield should poll or await a few frames after this method
## before freeing the tree or calling [method peer_reset_state].
func close_channels() -> void:
	if _session:
		_session.close_channels()


func _build_session_and_signaler() -> void:
	_session = WebRTCSession.new()
	if not global_ice_servers.is_empty():
		_session.ice_servers = global_ice_servers
	else:
		_session.ice_servers = ice_servers
	_session.connect_retry = connect_retry
	_session.max_connect_attempts = max_connect_attempts
	_signaler = _make_signaler()

	_session.signal_out.connect(_signaler.send)
	_signaler.received.connect(_session.deliver)
	_session.native_connected.connect(_on_native_connected)
	_session.native_connected.connect(_signaler.on_session_connected)
	_session.native_disconnected.connect(_on_native_disconnected)
	_signaler.ready.connect(func() -> void: signaling_connected.emit())
	_signaler.lost.connect(func() -> void: signaling_disconnected.emit())


func _clear_session_and_signaler() -> void:
	if _session:
		_session.close()
	_session = null
	if _signaler:
		_signaler.close()
	_signaler = null


func _on_native_connected(id: int) -> void:
	Netw.dbg.info("WebRTC native connection established with id %d.", [id])


func _on_native_disconnected(id: int) -> void:
	Netw.dbg.info("WebRTC native connection lost with id %d.", [id])


## Returns the active room id, or the parent default.
func get_join_address() -> String:
	if _signaler and not _signaler.room_id().is_empty():
		return _signaler.room_id()
	return super.get_join_address()


## Returns a [code]"Room Hash"[/code] [AddressHint].
func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Room Hash",
		"20-char hex",
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


## Budgets the connect timeout for the retry-aware WebRTC join, so a stalled
## attempt can re-offer within the window rather than failing on the first try.
func connect_timeout_hint() -> float:
	return connect_retry * float(max_connect_attempts) + 4.0


## Preserves authored WebRTC settings after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	if source is WebRTCBackend:
		var other := source as WebRTCBackend
		server_name = other.server_name
		ice_servers = other.ice_servers.duplicate(true)
		connect_retry = other.connect_retry
		max_connect_attempts = other.max_connect_attempts


## Clears the active session and signaler.
func peer_reset_state() -> void:
	Netw.dbg.trace("WebRTCBackend: resetting peer state.")
	_clear_session_and_signaler()
	_is_server = false


## Returns the display name for this backend.
func get_display_name() -> String:
	return "WebRTC"
