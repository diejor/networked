## Wraps the optional Nakama addon.
##
## Projects still load when [code]com.heroiclabs.nakama[/code] is not
## installed. Check [method is_addon_present] before calling anything else.
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

const _FACADE_PATH := "res://addons/com.heroiclabs.nakama/Nakama.gd"
const _NAKAMA_LOG_LEVEL_WARNING := 2

## The Nakama storage collection where lobby listings are saved.
const LOBBY_COLLECTION := "lobbies"

static var _class_cache: Dictionary = { }

## Optional [Callable] that sends Nakama traffic through a proxy, such as a
## Discord activity proxy. It is called as
## [code]proxy_base_resolver(host_node, config_host)[/code] and returns the
## proxy address, such as [code]"app.discordsays.com/.proxy/nakama"[/code], or
## [code]""[/code] to connect directly.
static var proxy_base_resolver: Callable


static func _resolve_proxy_base(host_node: Node, config_host: String) -> String:
	if proxy_base_resolver.is_valid():
		return String(proxy_base_resolver.call(host_node, config_host))
	return ""


static func _apply_proxy_base(target: Object, base: String, scheme: String) -> void:
	if target != null and not base.is_empty():
		target._base_uri = "%s://%s" % [scheme, base]


# The browser already decompresses gzip, so the SDK decompressing it again
# fails. See godot#116574.
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

## Emitted when the match is joined and this peer has its id.
signal match_joined()

## Emitted when creating or joining a match fails, with the Nakama error
## [param message].
signal match_join_error(message: String)

## Emitted when the connection to Nakama closes.
signal socket_closed()

var _facade
var _client
var _session
var _socket
var _bridge

var _shared_session: NakamaSessionService


## Uses the account of [param session] for login and storage. Call it before
## [method connect_async]. Otherwise the wrapper logs in on its own.
func use_session(session: NakamaSessionService) -> void:
	_shared_session = session


var _connecting := false
signal _connect_finished(result: Dictionary)


## Returns [code]true[/code] when the Nakama addon is installed. Check it
## before calling other methods.
static func is_addon_present() -> bool:
	return _nakama_class("NakamaClient") != null


## Logs in to Nakama and opens the realtime connection. A helper node is
## added under [param host].
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

	if is_ready():
		return { "ok": true, "error": "" }

	if _connecting:
		return await _connect_finished

	_connecting = true
	var result := await _perform_connect(host, config)
	_connecting = false
	_connect_finished.emit(result)
	return result


func _perform_connect(host: Node, config: Dictionary) -> Dictionary:
	if _shared_session != null:
		_shared_session.configure(config)
		var auth := await _shared_session.connect_async()
		if not auth.ok:
			return auth
		_facade = null
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


func _build_bridge() -> void:
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


## Saves [param value] under [param collection] and [param key]. Everyone can
## read it, and only this user can change it. Each user has their own object
## for a key, and [method list_public_storage] returns all of them.
func write_public_storage(collection: String, key: String, value: Dictionary) -> bool:
	if collection.is_empty() or key.is_empty():
		return false
	# permission_read 2 = public, permission_write 1 = owner only.
	var answer := await write_storage_objects(
		[
			{
				"collection": collection,
				"key": key,
				"read": 2,
				"write": 1,
				"value": JSON.stringify(value),
			},
		],
	)
	return int(answer["error"]) == OK


## Reads public objects under [param collection] across all owners.
##
## Objects with the same key keep one value. Use [method list_public_storage]
## when the owner matters. Empty before the session is open.
## [codeblock]
## Dictionary
## └── key (String)
##     └── value (Dictionary)
## [/codeblock]
func read_public_storage(collection: String, limit := 100) -> Dictionary:
	var out := { }
	if collection.is_empty():
		return out
	var answer := await list_storage_objects(collection, limit)
	if int(answer["error"]) != OK:
		return out
	for object in answer["objects"]:
		var parsed: Variant = object["value"]
		if typeof(parsed) == TYPE_DICTIONARY:
			out[String(object["key"])] = parsed
	return out


## Lists public objects under [param collection] across all owners.
##
## Keeps every owner's object, including several with the same key.
## [param limit] is the page size, and every page is read.
## [codeblock]
## Array
## └── Dictionary
##     ├── key (String)
##     ├── value (Variant)
##     └── user_id (String)
## [/codeblock]
func list_public_storage(collection: String, limit := 100) -> Array:
	var out: Array = []
	if collection.is_empty():
		return out
	var cursor := ""
	while true:
		var answer := await list_storage_objects(collection, limit, cursor)
		if int(answer["error"]) != OK:
			return out
		var objects: Array = answer["objects"]
		out.append_array(objects)
		var next := String(answer["cursor"])
		if next.is_empty() or objects.is_empty():
			break
		cursor = next
	return out


## Deletes the caller-owned object under [param collection] and [param key].
##
## Deleting an object that does not exist succeeds.
func delete_public_storage(collection: String, key: String) -> void:
	if collection.is_empty() or key.is_empty():
		return
	await delete_storage_objects([{ "collection": collection, "key": key }])


## Writes a public relay lobby card keyed by [param match_id].
##
## Relay matches carry no lobby metadata, so the host stores it in
## [constant LOBBY_COLLECTION] and [method read_lobby_cards] reads it back.
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


## Returns the result every storage method on this wrapper answers.
##
## [param uncertain] is true when the request was sent but Nakama never
## confirmed whether it applied.
## [codeblock]
## Dictionary
## ├── error (int)        # @GlobalScope.Error
## ├── detail (String)
## └── uncertain (bool)
## [/codeblock]
static func storage_answer(
		error: int,
		detail: String,
		uncertain: bool,
) -> Dictionary:
	return { "error": error, "detail": detail, "uncertain": uncertain }


## Returns the result for a request that was never sent.
static func unsent(error: int, detail: String) -> Dictionary:
	return storage_answer(error, detail, false)


## Returns the result for a request made without an authenticated session.
static func unauthenticated() -> Dictionary:
	return unsent(ERR_UNAUTHORIZED, "no authenticated Nakama session")


# Translates a Nakama exception into an engine error, an explanation a person
# can read, and whether the request it belongs to may still have applied.
static func _storage_fault(exception: Variant) -> Dictionary:
	if exception == null:
		return storage_answer(ERR_CONNECTION_ERROR, "Nakama answered nothing", true)
	var detail := String(exception.message)
	if bool(exception.cancelled):
		return storage_answer(ERR_TIMEOUT, detail, true)
	var status := int(exception.status_code)
	if status < 0:
		return storage_answer(ERR_TIMEOUT, detail, true)
	if status == 401 or status == 403:
		return storage_answer(ERR_UNAUTHORIZED, detail, false)
	if status >= 500:
		return storage_answer(ERR_CONNECTION_ERROR, detail, true)
	if status >= 400:
		return storage_answer(ERR_INVALID_DATA, detail, false)
	return storage_answer(FAILED, detail, true)


# Adds the rows a read answered to a storage answer.
static func _with_objects(answer: Dictionary, objects: Array) -> Dictionary:
	answer["objects"] = objects
	return answer


# Adds the rows and continuation cursor a list answered to a storage answer.
static func _with_page(
		answer: Dictionary,
		objects: Array,
		cursor: String,
) -> Dictionary:
	answer["objects"] = objects
	answer["cursor"] = cursor
	return answer


## Returns the authenticated user's id, or an empty [String] before auth.
func own_user_id() -> String:
	var session = _resolve_session()
	return String(session.user_id) if session != null else ""


## Writes a batch of storage [param objects] in one call.
##
## Does not need a match socket. Returns the result of [method storage_answer].
## [codeblock]
## Array
## └── Dictionary
##     ├── collection (String)
##     ├── key (String)
##     ├── value (String)   # JSON string.
##     ├── read (int)       # Optional. Default 1.
##     └── write (int)      # Optional. Default 1.
## [/codeblock]
func write_storage_objects(objects: Array) -> Dictionary:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null:
		return unauthenticated()
	if objects.is_empty():
		return storage_answer(OK, "", false)
	var write_script: Variant = _nakama_class("NakamaWriteStorageObject")
	if write_script == null:
		return unsent(ERR_UNAVAILABLE, "the Nakama addon is absent")
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
	if res == null:
		return storage_answer(ERR_CONNECTION_ERROR, "Nakama answered nothing", true)
	if res.is_exception():
		return _storage_fault(res.get_exception())
	return storage_answer(OK, "", false)


## Reads a batch of storage objects named by [param ids].
##
## [code]user_id[/code] defaults to the session user.
## [codeblock]
## ids (Array)
## └── Dictionary
##     ├── collection (String)
##     ├── key (String)
##     └── user_id (String)
## [/codeblock]
## Returns the result of [method storage_answer] with the objects found. An
## object that is not stored is left out of [code]objects[/code].
## [codeblock]
## Dictionary
## ├── error (int)
## ├── detail (String)
## ├── uncertain (bool)
## └── objects (Array)
##     └── Dictionary
##         ├── collection (String)
##         ├── key (String)
##         ├── user_id (String)
##         └── value (Variant)
## [/codeblock]
func read_storage_objects(ids: Array) -> Dictionary:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null:
		return _with_objects(
			unauthenticated(),
			[],
		)
	if ids.is_empty():
		return _with_objects(storage_answer(OK, "", false), [])
	var id_script: Variant = _nakama_class("NakamaStorageObjectId")
	if id_script == null:
		return _with_objects(
			unsent(ERR_UNAVAILABLE, "the Nakama addon is absent"),
			[],
		)
	var own := own_user_id()
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
	if res == null:
		return _with_objects(
			storage_answer(ERR_CONNECTION_ERROR, "Nakama answered nothing", false),
			[],
		)
	if res.is_exception():
		return _with_objects(_storage_fault(res.get_exception()), [])
	var out: Array = []
	for object in res.objects:
		out.append(
			{
				"collection": String(object.collection),
				"key": String(object.key),
				"user_id": String(object.user_id),
				"value": JSON.parse_string(String(object.value)),
			},
		)
	return _with_objects(storage_answer(OK, "", false), out)


## Lists one page of [param collection], owned by [param owner].
##
## An empty [param owner] lists every owner's objects. Pass [method own_user_id]
## for the session user's objects only. Pass the returned cursor back as
## [param cursor] for the next page. The listing is done when the cursor is
## empty.
## [codeblock]
## Dictionary
## ├── error (int)
## ├── detail (String)
## ├── uncertain (bool)
## ├── objects (Array)
## │   └── Dictionary
## │       ├── key (String)
## │       ├── user_id (String)
## │       └── value (Variant)
## └── cursor (String)
## [/codeblock]
func list_storage_objects(
		collection: String,
		limit := 100,
		cursor := "",
		owner := "",
) -> Dictionary:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null:
		return _with_page(
			unauthenticated(),
			[],
			"",
		)
	var res = await client.list_storage_objects_async(
		session,
		collection,
		owner,
		limit,
		cursor,
	)
	if res == null:
		return _with_page(
			storage_answer(ERR_CONNECTION_ERROR, "Nakama answered nothing", false),
			[],
			"",
		)
	if res.is_exception():
		return _with_page(_storage_fault(res.get_exception()), [], "")
	var objects: Array = []
	for object in res.objects:
		objects.append(
			{
				"key": String(object.key),
				"user_id": String(object.user_id),
				"value": JSON.parse_string(String(object.value)),
			},
		)
	var next := String(res.cursor) if res.cursor != null else ""
	return _with_page(storage_answer(OK, "", false), objects, next)


## Deletes a batch of storage objects named by [param ids].
##
## Deleting an object that does not exist succeeds. Returns the result of
## [method storage_answer].
## [codeblock]
## Array
## └── Dictionary
##     ├── collection (String)
##     └── key (String)
## [/codeblock]
func delete_storage_objects(ids: Array) -> Dictionary:
	var client = _resolve_client()
	var session = _resolve_session()
	if client == null or session == null:
		return unauthenticated()
	if ids.is_empty():
		return storage_answer(OK, "", false)
	var id_script: Variant = _nakama_class("NakamaStorageObjectId")
	if id_script == null:
		return unsent(ERR_UNAVAILABLE, "the Nakama addon is absent")
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
	if res == null:
		return storage_answer(ERR_CONNECTION_ERROR, "Nakama answered nothing", true)
	if res.is_exception():
		return _storage_fault(res.get_exception())
	return storage_answer(OK, "", false)


# Resolves the active client, preferring the shared session when bound.
func _resolve_client():
	return _shared_session.client() if _shared_session != null else _client


# Resolves the active session, preferring the shared session when bound.
func _resolve_session():
	return _shared_session.session() if _shared_session != null else _session


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
