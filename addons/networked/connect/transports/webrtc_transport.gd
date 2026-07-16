## A [NetwTransport] for peer-to-peer WebRTC rooms, signaling behind a
## [WebRTCSignaler].
##
## Recognizes the [code]&"webrtc"[/code] scheme and builds a [WebRTCPeerView]
## that owns the [WebRTCSession] and the signaler a subclass supplies through
## [method _make_signaler]. The transport holds only authored configuration and
## the process-global ICE cache, so it stays stateless. [TrackerWebRTCTransport]
## is the default WebTorrent implementation.
@abstract
class_name WebRTCTransport
extends NetwTransport

## Globally cached ICE servers, shared across every transport instance.
static var global_ice_servers: Array[Dictionary] = []

## Optional namespace isolating signaling and room codes on public networks.
var signaling_namespace: String = ""

## Character set for short room codes, excluding ambiguous glyphs.
var room_code_characters: String = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

## ICE server definitions passed to each [WebRTCPeerConnection].
var ice_servers: Array[Dictionary] = [
	{ "urls": ["stun:stun.l.google.com:19302"] },
	{
		"urls": ["turn:openrelay.metered.ca:80"],
		"username": "openrelayproject",
		"credential": "openrelayproject",
	},
]

## Seconds a joining client waits for the native link before re-sending its offer.
var connect_retry: float = 8.0

## Offer attempts a joining client makes before leaving failure to the budget.
var max_connect_attempts: int = 3

## Seconds the session waits for ICE gathering before the final top-up.
var gather_timeout: float = 6.0

## Minimum seconds between candidate top-up bundles while ICE gathers.
var topup_interval: float = 0.25

## Filters TURN over TCP/TLS on native platforms, where libjuice is UDP-only.
var filter_unsupported_turn: bool = true


## Builds the [WebRTCSignaler] this transport signals through.
@abstract
func _make_signaler() -> WebRTCSignaler


func scheme() -> StringName:
	return &"webrtc"


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"webrtc"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.scheme == &"webrtc"


func _can_view(peer: MultiplayerPeer) -> bool:
	return peer is WebRTCMultiplayerPeer


func _host(
		attempt: NetwConnectAttempt,
		_config: NetwHostConfig,
) -> MultiplayerPeer:
	await _ensure_ice_servers()
	var view := WebRTCPeerView.new(self, _make_signaler(), attempt)
	attempt.context["webrtc_view"] = view
	return view.open_host(null)


func _join(
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
) -> MultiplayerPeer:
	await _ensure_ice_servers()
	var view := WebRTCPeerView.new(self, _make_signaler(), attempt)
	attempt.context["webrtc_view"] = view
	return view.open_client(target.address)


func _make_view(
		_peer: MultiplayerPeer,
		attempt: NetwConnectAttempt = null,
) -> NetwPeerView:
	return attempt.context.get("webrtc_view") if attempt else null


func _address_hint() -> NetwAddressHint:
	var placeholder := "5-char code" if not signaling_namespace.is_empty() else "20-char hex"
	return NetwAddressHint.make(
		"Room ID",
		placeholder,
		"Room identifier copied from the host (auto-copied to clipboard on host).",
		false,
		false,
	)


func _timeout_hint(_target: NetwConnectTarget) -> float:
	return gather_timeout + connect_retry * float(max_connect_attempts) + 4.0


func _display_name() -> String:
	return "WebRTC"


func _capabilities() -> int:
	return Capability.LISTEN_FALLBACK


## Returns the cached credentials when fetched, else the authored servers.
func effective_ice_servers() -> Array[Dictionary]:
	return global_ice_servers if not global_ice_servers.is_empty() else ice_servers


# Fetches ephemeral TURN credentials once when the project configures a URL.
func _ensure_ice_servers() -> void:
	if Netw.is_test_env() or not global_ice_servers.is_empty():
		return
	var url := ""
	if ProjectSettings.has_setting("networked/webrtc/turn_credentials_url"):
		url = ProjectSettings.get_setting("networked/webrtc/turn_credentials_url")
	if url.is_empty():
		return
	var headers: PackedStringArray = []
	if ProjectSettings.has_setting("networked/webrtc/turn_credentials_headers"):
		headers = ProjectSettings.get_setting("networked/webrtc/turn_credentials_headers")
	# TODO: fetch through an HTTPClient pumped from poll so the transport never
	# touches a scene tree. The main-loop root keeps behavior identical here.
	var root := Engine.get_main_loop().root as Window
	if root == null:
		return
	var http := HTTPRequest.new()
	http.timeout = 5.0
	root.add_child(http)
	var err := http.request(url, headers)
	if err != OK:
		http.queue_free()
		return
	var results: Array = await http.request_completed
	http.queue_free()
	var status_code: int = results[1]
	var body: PackedByteArray = results[3]
	if status_code != 200:
		Netw.dbg.warn("WebRTC credentials fetch failed (code %d).", [status_code])
		return
	var parsed: Variant = JSON.parse_string(body.get_string_from_utf8())
	if typeof(parsed) == TYPE_ARRAY:
		var servers: Array[Dictionary] = []
		servers.assign(parsed)
		global_ice_servers = servers


## Filters TURN TCP/TLS servers unsupported by native libjuice.
static func filter_ice_servers(servers: Array[Dictionary]) -> Array[Dictionary]:
	if OS.has_feature("web"):
		return servers
	var filtered: Array[Dictionary] = []
	filtered.assign(servers.map(_map_ice_server).filter(_is_ice_server_valid))
	return filtered


static func _map_ice_server(server: Dictionary) -> Dictionary:
	var urls := server.get("urls") as Array
	assert(urls != null, "ICE server configuration is missing 'urls' key.")
	var clean_urls := urls.filter(_is_url_supported_native)
	if clean_urls.is_empty():
		return { }
	var copy := server.duplicate()
	copy["urls"] = clean_urls
	return copy


static func _is_ice_server_valid(server: Dictionary) -> bool:
	return not server.is_empty()


static func _is_url_supported_native(url: String) -> bool:
	var u := url.to_lower().strip_edges()
	return not (
			u.begins_with("turns:")
			or u.contains("transport=tcp")
			or u.contains("transport=tls")
	)


## Registers [param room_id] as hosted locally on this machine.
static func register_local_room(room_id: String) -> void:
	if room_id.is_empty():
		return
	var rooms := _read_local_rooms()
	if not rooms.has(room_id):
		rooms.append(room_id)
	_write_local_rooms(rooms)


## Removes [param room_id] from the local room registry.
static func unregister_local_room(room_id: String) -> void:
	if room_id.is_empty():
		return
	var rooms := _read_local_rooms()
	rooms.erase(room_id)
	if rooms.is_empty():
		DirAccess.remove_absolute(_LOCAL_ROOMS_PATH)
	else:
		_write_local_rooms(rooms)


## Returns [code]true[/code] when [param room_id] was hosted locally.
static func is_local_room(room_id: String) -> bool:
	return not room_id.is_empty() and _read_local_rooms().has(room_id)


const _LOCAL_ROOMS_PATH := "user://local_webrtc_rooms.txt"


static func _read_local_rooms() -> Array[String]:
	var rooms: Array[String] = []
	if not FileAccess.file_exists(_LOCAL_ROOMS_PATH):
		return rooms
	var file := FileAccess.open(_LOCAL_ROOMS_PATH, FileAccess.READ)
	if file:
		while not file.eof_reached():
			var line := file.get_line().strip_edges()
			if not line.is_empty():
				rooms.append(line)
	return rooms


static func _write_local_rooms(rooms: Array[String]) -> void:
	var file := FileAccess.open(_LOCAL_ROOMS_PATH, FileAccess.WRITE)
	if file:
		for r in rooms:
			file.store_line(r)
