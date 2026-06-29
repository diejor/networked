## Parse-safe boundary around the optional Nakama addon.
##
## [method is_addon_present] is the availability gate. No vendor API type is
## named before that gate, so projects still load when
## [code]com.heroiclabs.nakama[/code] is absent.
##
## [br][br]
## [method connect_async] creates or reuses a [NakamaSessionService] session, then
## [NakamaRelayBridge] turns the realtime socket into a [MultiplayerPeer].
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
static var _class_cache: Dictionary = { }

## Optional hook that routes Nakama traffic through a platform proxy.
##
## [method connect_async] applies the returned base to both the HTTP client and
## realtime socket. Return [code]""[/code] to keep the configured Nakama host.
## [codeblock]
## Callable
## ├── host_node (Node)
## ├── config_host (String)
## └── return (String)
##     ├── ""                                  # direct
##     └── "app.discordsays.com/.proxy/nakama" # proxied
## [/codeblock]
static var proxy_base_resolver: Callable


# Returns the proxy base for this connection, or "" when no resolver is set or it
# declines. host_node anchors the resolver's tree/service lookup; config_host is
# the configured Nakama host it may key off.
static func _resolve_proxy_base(host_node: Node, config_host: String) -> String:
	if proxy_base_resolver.is_valid():
		return String(proxy_base_resolver.call(host_node, config_host))
	return ""


# Rewrites the _base_uri of a Nakama client api or socket to scheme://base. A
# no-op when base is empty, so the default direct connection is untouched. This
# is the one place that reaches into the vendor object's private _base_uri.
static func _apply_proxy_base(target: Object, base: String, scheme: String) -> void:
	if target != null and not base.is_empty():
		target._base_uri = "%s://%s" % [scheme, base]


# Workaround for Godot HTML5 export bug (godot#116574): the browser transparently
# decompresses, so the SDK's own gzip decode double-decompresses and fails. Off
# the web this is a no-op. Reaches into the vendor client's http adapter.
static func _disable_web_gzip(client: Object) -> void:
	if client == null:
		return
	if not (OS.has_feature("web") or OS.get_name() == "Web"):
		return
	client._api_client._http_adapter.child_entered_tree.connect(
		func(node: Node) -> void:
			if node is HTTPRequest:
				node.accept_gzip = false
	)

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

# Nakama handles. When _shared_session is set, _facade/_client/_session belong
# to it and are read, not owned, by this wrapper.
var _facade
var _client
var _session
var _socket
var _bridge

# Optional shared authentication. When present, auth and storage route through
# this session's account and the relay socket is built from its client. When
# null, the wrapper self-authenticates (the standalone, pre-session path).
var _shared_session: NakamaSessionService


## Binds this wrapper to a shared [NakamaSessionService].
##
## Call before [method connect_async]. A bound wrapper reuses the shared account
## for auth and storage. An unbound wrapper creates its own device session.
func use_session(session: NakamaSessionService) -> void:
	_shared_session = session

# Single-flight guard for connect_async. While a connect is in flight, late
# callers await _connect_finished instead of starting a second connect that
# would clobber the shared handles and orphan the first one's pending await.
var _connecting := false
signal _connect_finished(result: Dictionary)


## Returns [code]true[/code] when the Nakama addon scripts are installed.
##
## This is the only public availability gate. Call it before using other
## [NakamaWrapper] methods in code that may run without the optional addon.
static func is_addon_present() -> bool:
	return _nakama_class("NakamaClient") != null


## Authenticates a device session and opens the realtime socket under
## [param host].
##
## [param config] carries the connection fields. The facade node is parented to
## [param host] so the socket adapter self-polls inside the live tree.
## [codeblock]
## Dictionary
## ├── server_key (String)
## ├── host (String)
## ├── port (int)
## ├── use_ssl (bool)
## ├── timeout (int)
## ├── device_id (String)
## └── username (String)
##
## Returns
## ├── ok (bool)
## └── error (String)
## [/codeblock]
func connect_async(host: Node, config: Dictionary) -> Dictionary:
	if not is_addon_present():
		return { "ok": false, "error": "Nakama addon not present" }
	if not is_instance_valid(host):
		return { "ok": false, "error": "Invalid host node" }

	# Already connected: hand back the live session without rebuilding it.
	if is_ready():
		return { "ok": true, "error": "" }

	# A connect is already running: wait for it rather than starting a second
	# one. Concurrent callers (the host path and a lobby-browse refresh) would
	# otherwise both build a facade and overwrite each other's handles, leaving
	# the first caller awaiting an orphaned client forever.
	if _connecting:
		return await _connect_finished

	_connecting = true
	var result := await _perform_connect(host, config)
	_connecting = false
	_connect_finished.emit(result)
	return result


func _perform_connect(host: Node, config: Dictionary) -> Dictionary:
	# Shared-session path: auth and socket come from the one account.
	if _shared_session != null:
		_shared_session.configure(config)
		var auth := await _shared_session.connect_async()
		if not auth.ok:
			return auth
		_facade = null # owned by the shared session, not freed by leave()
		_client = _shared_session.client()
		_session = _shared_session.session()
		_socket = _shared_session.create_socket()
		await _socket.connect_async(_session)
		if not _socket.is_connected_to_host():
			return { "ok": false, "error": "Nakama socket failed to connect" }
		_build_bridge()
		return { "ok": true, "error": "" }

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

	_disable_web_gzip(_client)
	var proxy_base := _resolve_proxy_base(host, String(config.get("host", "127.0.0.1")))
	_apply_proxy_base(_client._api_client, proxy_base, "https")

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
	_apply_proxy_base(_socket, proxy_base, "wss")
	await _socket.connect_async(_session)
	if not _socket.is_connected_to_host():
		return { "ok": false, "error": "Nakama socket failed to connect" }

	_build_bridge()
	return { "ok": true, "error": "" }


# Wires the relay bridge over the open socket and re-emits its lifecycle signals.
func _build_bridge() -> void:
	# NakamaRelayBridge is a networked class that names no Nakama type, so the
	# wrapper references it directly instead of loading it by path.
	_bridge = NakamaRelayBridge.new(_socket)
	_bridge.match_joined.connect(func() -> void: match_joined.emit())
	_bridge.match_join_error.connect(_on_bridge_join_error)
	_socket.closed.connect(func() -> void: socket_closed.emit())


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


## Resolves [param peer_id] to the joining Nakama user id, or an empty string.
func user_id_for_peer(peer_id: int) -> String:
	if _bridge == null:
		return ""
	var presence: Variant = _bridge.get_user_presence_for_peer(peer_id)
	if presence == null:
		return ""
	return String(presence.user_id)


## Lists active relay matches.
##
## Relay matches are listed with [code]authoritative = false[/code]. Returns an
## empty [Array] before [method connect_async] opens a session.
## [codeblock]
## Array
## └── match
##     ├── match_id
##     └── size
## [/codeblock]
func list_matches(min_size := 0, max_size := 100, limit := 100) -> Array:
	if _client == null or _session == null:
		return []
	var res = await _client.list_matches_async(
		_session,
		min_size,
		max_size,
		limit,
		false,
		"",
		"",
	)
	if res == null or res.is_exception():
		return []
	return res.matches


## Writes a public-read object under [param collection] and [param key].
##
## Any session can read the object. Only the writer can overwrite it. Nakama
## scopes storage by collection, key, and owner, so the same key can exist once
## per user.
## [codeblock]
## Storage object
## ├── collection = collection
## ├── key = key
## ├── owner = session user id
## ├── read = public
## └── write = owner only
## [/codeblock]
## [method list_public_storage] preserves every owner entry. Use it when
## concurrent writers can publish the same key.
func write_public_storage(collection: String, key: String, value: Dictionary) -> bool:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or collection.is_empty() or key.is_empty():
		return false
	var write_script: Variant = _nakama_class("NakamaWriteStorageObject")
	if write_script == null:
		return false
	# permission_read 2 = public, permission_write 1 = owner only.
	var obj: Variant = write_script.new(
		collection,
		key,
		2,
		1,
		JSON.stringify(value),
		"",
	)
	var res = await client.write_storage_objects_async(session, [obj])
	return res != null and not res.is_exception()


## Reads public objects under [param collection] across all owners.
##
## This collapses same-key objects to one value. Use [method list_public_storage]
## when the owner matters. Empty before the session is open.
## [codeblock]
## Dictionary
## └── key (String)
##     └── value (Dictionary)
## [/codeblock]
func read_public_storage(collection: String, limit := 100) -> Dictionary:
	var out := { }
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or collection.is_empty():
		return out
	var res = await client.list_storage_objects_async(
		session,
		collection,
		"",
		limit,
	)
	if res == null or res.is_exception():
		return out
	for object in res.objects:
		var parsed: Variant = JSON.parse_string(String(object.value))
		if typeof(parsed) == TYPE_DICTIONARY:
			out[String(object.key)] = parsed
	return out


## Lists public objects under [param collection] across all owners.
##
## Nakama scopes storage by collection, key, and owner. This preserves every
## owner entry, including multiple objects with the same key.
## [codeblock]
## Array
## └── Dictionary
##     ├── key (String)
##     ├── value (Variant)
##     └── user_id (String)
## [/codeblock]
func list_public_storage(collection: String, limit := 100) -> Array:
	var out: Array = []
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or collection.is_empty():
		return out
	var res = await client.list_storage_objects_async(
		session,
		collection,
		"",
		limit,
	)
	if res == null or res.is_exception():
		return out
	for object in res.objects:
		out.append(
			{
				"key": String(object.key),
				"value": JSON.parse_string(String(object.value)),
				"user_id": String(object.user_id),
			},
		)
	return out


## Deletes the caller-owned object under [param collection] and [param key].
##
## The operation is best effort and idempotent on the server side.
func delete_public_storage(collection: String, key: String) -> void:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or collection.is_empty() or key.is_empty():
		return
	var id_script: Variant = _nakama_class("NakamaStorageObjectId")
	if id_script == null:
		return
	var id: Variant = id_script.new(collection, key, "", "")
	await client.delete_storage_objects_async(session, [id])


## Writes a public relay lobby card keyed by [param match_id].
##
## Relay matches do not carry browse metadata. The host stores that metadata in
## [constant LOBBY_COLLECTION], and [method read_lobby_cards] reads it back.
## [method write_lobby_card] is a typed alias over [method write_public_storage].
func write_lobby_card(match_id: String, card: Dictionary) -> bool:
	return await write_public_storage(LOBBY_COLLECTION, match_id, card)


## Reads public relay lobby cards from [constant LOBBY_COLLECTION].
##
## Empty before the session is open.
## [codeblock]
## Dictionary
## └── match_id (String)
##     └── card (Dictionary)
## [/codeblock]
func read_lobby_cards(limit := 100) -> Dictionary:
	return await read_public_storage(LOBBY_COLLECTION, limit)


## Deletes the local lobby card keyed by [param match_id].
func delete_lobby_card(match_id: String) -> void:
	await delete_public_storage(LOBBY_COLLECTION, match_id)

# Generic storage objects.


## Writes a batch of storage [param objects] in one call.
##
## Resolves the client from [NakamaSessionService] when bound, so a storage-only
## wrapper never opens a match socket.
## [codeblock]
## Array
## └── Dictionary
##     ├── collection (String)
##     ├── key (String)
##     ├── value (String)   # JSON string.
##     ├── read (int)       # Optional. Default 1.
##     └── write (int)      # Optional. Default 1.
## [/codeblock]
func write_storage_objects(objects: Array) -> bool:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or objects.is_empty():
		return false
	var write_script: Variant = _nakama_class("NakamaWriteStorageObject")
	if write_script == null:
		return false
	var payload: Array = []
	for entry in objects:
		payload.append(
			write_script.new(
				String(entry.get("collection", "")),
				String(entry.get("key", "")),
				int(entry.get("read", 1)),
				int(entry.get("write", 1)),
				String(entry.get("value", "")),
				"",
			),
		)
	var res = await client.write_storage_objects_async(session, payload)
	return res != null and not res.is_exception()


## Reads a batch of storage objects named by [param ids].
##
## [code]user_id[/code] defaults to the session user. Empty before the session is
## authenticated.
## [codeblock]
## ids (Array)
## └── Dictionary
##     ├── collection (String)
##     ├── key (String)
##     └── user_id (String)
##
## Returns (Array)
## └── Dictionary
##     ├── collection (String)
##     ├── key (String)
##     └── value (Variant)
## [/codeblock]
func read_storage_objects(ids: Array) -> Array:
	var out: Array = []
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or ids.is_empty():
		return out
	var id_script: Variant = _nakama_class("NakamaStorageObjectId")
	if id_script == null:
		return out
	var own := _own_user_id()
	var query: Array = []
	for entry in ids:
		query.append(
			id_script.new(
				String(entry.get("collection", "")),
				String(entry.get("key", "")),
				String(entry.get("user_id", own)),
				"",
			),
		)
	var res = await client.read_storage_objects_async(session, query)
	if res == null or res.is_exception():
		return out
	for object in res.objects:
		out.append(
			{
				"collection": String(object.collection),
				"key": String(object.key),
				"value": JSON.parse_string(String(object.value)),
			},
		)
	return out


## Lists every storage object under [param collection] for the session user.
##
## Pass [param cursor] to page. Empty before the session is authenticated.
## [codeblock]
## Dictionary
## ├── objects (Array)
## │   └── Dictionary
## │       ├── key (String)
## │       └── value (Variant)
## └── cursor (String)
## [/codeblock]
func list_storage_objects(collection: String, limit := 100, cursor := "") -> Dictionary:
	var out := { "objects": [], "cursor": "" }
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null:
		return out
	var res = await client.list_storage_objects_async(
		session,
		collection,
		_own_user_id(),
		limit,
		cursor,
	)
	if res == null or res.is_exception():
		return out
	var objects: Array = []
	for object in res.objects:
		objects.append(
			{
				"key": String(object.key),
				"value": JSON.parse_string(String(object.value)),
			},
		)
	out["objects"] = objects
	out["cursor"] = String(res.cursor) if res.cursor != null else ""
	return out


## Deletes a batch of storage objects named by [param ids].
##
## The operation is idempotent on the server side.
## [codeblock]
## Array
## └── Dictionary
##     ├── collection (String)
##     └── key (String)
## [/codeblock]
func delete_storage_objects(ids: Array) -> bool:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null or ids.is_empty():
		return false
	var id_script: Variant = _nakama_class("NakamaStorageObjectId")
	if id_script == null:
		return false
	var payload: Array = []
	for entry in ids:
		payload.append(
			id_script.new(
				String(entry.get("collection", "")),
				String(entry.get("key", "")),
				"",
				"",
			),
		)
	var res = await client.delete_storage_objects_async(session, payload)
	return res != null and not res.is_exception()


# Resolves the active client, preferring the shared session when bound.
func _resolve_client():
	return _shared_session.client() if _shared_session != null else _client


# Resolves the active session, preferring the shared session when bound.
func _resolve_session():
	return _shared_session.session() if _shared_session != null else _session


# Returns the authenticated user's id, or an empty string before auth.
func _own_user_id() -> String:
	var session = _resolve_session()
	return String(session.user_id) if session != null else ""


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
