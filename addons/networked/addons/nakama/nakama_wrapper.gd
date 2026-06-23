## Parse-safe boundary around the optional [code]com.heroiclabs.nakama[/code]
## addon.
##
## No Nakama addon type is named at parse time. The facade is loaded by its
## script path, and addon API classes are resolved at runtime by their global
## class name, so the networked addon still parses when the Nakama addon is
## absent. [method is_addon_present] gates all other calls. The wrapper owns the
## authenticated socket and the [NakamaRelayBridge] that turns Nakama match data
## into a working [MultiplayerPeer], and it re-emits the two bridge lifecycle
## signals as its own.
## [codeblock]
## var wrapper := NakamaWrapper.new()
## if not NakamaWrapper.is_addon_present():
##     return
## var res := await wrapper.connect_async(host_node, {
##     "host": "relay.example.com", "port": 443, "use_ssl": true,
## })
## if res.ok:
##     wrapper.create_match()
##     await wrapper.match_joined
##     tree.api.multiplayer_peer = wrapper.peer()
## [/codeblock]
class_name NakamaWrapper

# The facade has no class_name and is not an autoload, so it is the one Nakama
# script the wrapper must reach by path. Every other addon type resolves by
# global class name through _nakama_class.
const _FACADE_PATH := "res://addons/com.heroiclabs.nakama/Nakama.gd"
const _NAKAMA_LOG_LEVEL_WARNING := 2

## Nakama storage collection the relay lobby browse cards are written under.
const LOBBY_COLLECTION := "lobbies"

# Global class name -> resolved Script (or null when absent), memoized across
# every wrapper instance so the class registry is scanned at most once per name.
static var _class_cache: Dictionary = {}

## Emitted once the local peer id is granted and the match is fully joined.
##
## For a host this fires right after [method create_match] resolves. For a
## client it fires when the host's peer id assignment arrives.
signal match_joined()

## Emitted when joining or creating a match fails. Carries the Nakama error
## [param message].
signal match_join_error(message: String)

## Emitted when the underlying socket closes, mirroring the host leaving or a
## transport drop.
signal socket_closed()

# Nakama handles.
var _facade
var _client
var _session
var _socket
var _bridge


## Returns [code]true[/code] when the Nakama addon scripts are installed.
##
## Probes the global class registry for a core Nakama class instead of a file
## path, mirroring [method SteamWrapper.is_available]. Both the backend
## availability gate and the directory bootstrap defer to this.
static func is_addon_present() -> bool:
	return _nakama_class("NakamaClient") != null


## Authenticates a device session and opens the realtime socket under
## [param host].
##
## [param config] keys: [code]server_key[/code], [code]host[/code],
## [code]port[/code], [code]use_ssl[/code], [code]timeout[/code],
## [code]device_id[/code], [code]username[/code]. Returns
## [code]{ ok: bool, error: String }[/code]. The facade node is parented to
## [param host] so the socket adapter self-polls inside the live tree.
func connect_async(host: Node, config: Dictionary) -> Dictionary:
	if not is_addon_present():
		return { "ok": false, "error": "Nakama addon not present" }
	if not is_instance_valid(host):
		return { "ok": false, "error": "Invalid host node" }

	var facade_script: Variant = load(_FACADE_PATH)
	_facade = facade_script.new()
	_facade.name = "NakamaFacade"
	host.add_child(_facade)

	var client_scheme := "https" if bool(config.get("use_ssl", false)) else "http"
	_client = _facade.create_client(
		String(config.get("server_key", "defaultkey")),
		String(config.get("host", "127.0.0.1")),
		int(config.get("port", 7350)),
		client_scheme,
		int(config.get("timeout", 3)),
		_NAKAMA_LOG_LEVEL_WARNING,
	)

	var device_id := String(config.get("device_id", ""))
	if device_id.is_empty():
		device_id = OS.get_unique_id()
	var username := String(config.get("username", ""))

	_session = await _client.authenticate_device_async(
		device_id,
		username if not username.is_empty() else null,
		true,
	)
	if _session.is_exception():
		return { "ok": false, "error": _session.get_exception().message }

	_socket = _facade.create_socket_from(_client)
	await _socket.connect_async(_session)
	if not _socket.is_connected_to_host():
		return { "ok": false, "error": "Nakama socket failed to connect" }

	# NakamaRelayBridge is a networked class that names no Nakama type, so the
	# wrapper references it directly instead of loading it by path.
	_bridge = NakamaRelayBridge.new(_socket)
	_bridge.match_joined.connect(func() -> void: match_joined.emit())
	_bridge.match_join_error.connect(_on_bridge_join_error)
	_socket.closed.connect(func() -> void: socket_closed.emit())
	return { "ok": true, "error": "" }


## Returns [code]true[/code] once [method connect_async] has an open socket and
## bridge.
func is_ready() -> bool:
	return _bridge != null and _socket != null and _socket.is_connected_to_host()


## Creates a relay match and claims host peer id [code]1[/code].
##
## Resolves [signal match_joined] on success or [signal match_join_error] on
## failure.
func create_match() -> void:
	if _bridge == null:
		match_join_error.emit("Nakama bridge not ready")
		return
	_bridge.create_match()


## Joins the relay match named [param match_id] and awaits a host-assigned peer
## id.
##
## Resolves [signal match_joined] on success or [signal match_join_error] on
## failure.
func join_match(match_id: String) -> void:
	if _bridge == null:
		match_join_error.emit("Nakama bridge not ready")
		return
	_bridge.join_match(match_id)


## Returns the [MultiplayerPeer] the bridge drives, or [code]null[/code] before
## [method connect_async].
func peer() -> MultiplayerPeer:
	if _bridge == null:
		return null
	return _bridge.multiplayer_peer as MultiplayerPeer


## Returns the active relay match id, or an empty string when not in a match.
func match_id() -> String:
	if _bridge == null:
		return ""
	return String(_bridge.match_id)


## Resolves [param peer_id] to the joining Nakama username, or an empty string.
func username_for_peer(peer_id: int) -> String:
	if _bridge == null:
		return ""
	var presence: Variant = _bridge.get_user_presence_for_peer(peer_id)
	if presence == null:
		return ""
	return String(presence.username)


## Lists active relay matches, returning match handles that expose
## [code]match_id[/code] and [code]size[/code].
##
## Browses with [code]authoritative = false[/code], which is the listing path
## relay matches fall under. Returns an empty array before the session is open.
func list_matches(min_size := 0, max_size := 100, limit := 100) -> Array:
	if _client == null or _session == null:
		return []
	var res = await _client.list_matches_async(
		_session, min_size, max_size, limit, false, "", "",
	)
	if res == null or res.is_exception():
		return []
	return res.matches


## Writes a public-read lobby card keyed by [param match_id] under
## [constant LOBBY_COLLECTION] and returns [code]true[/code] on success.
##
## Relay matches carry no metadata, so the host stores the browse card (name,
## host, capacity, tag) here for [method read_lobby_cards] to resolve. The
## object is public read and owner write.
func write_lobby_card(match_id: String, card: Dictionary) -> bool:
	if _client == null or _session == null or match_id.is_empty():
		return false
	var write_script: Variant = _nakama_class("NakamaWriteStorageObject")
	if write_script == null:
		return false
	# permission_read 2 = public, permission_write 1 = owner only.
	var obj: Variant = write_script.new(
		LOBBY_COLLECTION, match_id, 2, 1, JSON.stringify(card), "",
	)
	var res = await _client.write_storage_objects_async(_session, [obj])
	return res != null and not res.is_exception()


## Reads every public lobby card under [constant LOBBY_COLLECTION] into a
## [code]{ match_id: card_dict }[/code] map. Empty before the session is open.
func read_lobby_cards(limit := 100) -> Dictionary:
	var out := { }
	if _client == null or _session == null:
		return out
	var res = await _client.list_storage_objects_async(
		_session, LOBBY_COLLECTION, "", limit,
	)
	if res == null or res.is_exception():
		return out
	for object in res.objects:
		var parsed: Variant = JSON.parse_string(String(object.value))
		if typeof(parsed) == TYPE_DICTIONARY:
			out[String(object.key)] = parsed
	return out


## Best-effort delete of the local lobby card keyed by [param match_id].
func delete_lobby_card(match_id: String) -> void:
	if _client == null or _session == null or match_id.is_empty():
		return
	var id_script: Variant = _nakama_class("NakamaStorageObjectId")
	if id_script == null:
		return
	var id: Variant = id_script.new(LOBBY_COLLECTION, match_id, "", "")
	await _client.delete_storage_objects_async(_session, [id])


# Resolves a Nakama API class by its global name through the engine class
# registry, so the wrapper never hard-codes addon-internal script paths or names
# a Nakama type at parse time. Returns null when the addon is absent. Memoized in
# _class_cache, including the null miss, so the registry scan runs once per name.
static func _nakama_class(global_name: String) -> Variant:
	if _class_cache.has(global_name):
		return _class_cache[global_name]
	var resolved: Variant = null
	for entry in ProjectSettings.get_global_class_list():
		if String(entry.get("class", "")) == global_name:
			resolved = load(String(entry.get("path", "")))
			break
	_class_cache[global_name] = resolved
	return resolved


## Leaves the match and tears down the socket and facade node.
func leave() -> void:
	if _bridge != null:
		_bridge.leave()
	if _socket != null and _socket.is_connected_to_host():
		_socket.close()
	if is_instance_valid(_facade):
		_facade.queue_free()
	_facade = null
	_client = null
	_session = null
	_socket = null
	_bridge = null


func _on_bridge_join_error(exception: Variant) -> void:
	var message := "Nakama match join failed"
	if exception != null:
		message = String(exception.message)
	match_join_error.emit(message)
