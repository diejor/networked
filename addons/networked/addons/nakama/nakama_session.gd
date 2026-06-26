## Session-global Nakama authentication shared by every Nakama consumer.
##
## One [MultiplayerTree] has one authenticated Nakama account. Consumers resolve
## it through [method MultiplayerTree.get_nakama_session], so relay matches and
## storage calls use the same user.
## [codeblock]
## MultiplayerTree
## ├── NakamaSessionService
## │   ├── client
## │   └── session
## ├── NakamaLobbyDirectory
## │   └── create_socket()
## └── NakamaDatabase
##     └── session user storage
## [/codeblock]
class_name NakamaSessionService
extends NetwService

# The facade has no class_name and is not an autoload, so it is reached by path.
const _FACADE_PATH := "res://addons/com.heroiclabs.nakama/Nakama.gd"
const _NAKAMA_LOG_LEVEL_WARNING := 2

## Nakama server key, matching the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Relay host name or address, without scheme.
@export var host: String = "127.0.0.1"

## Relay port. Use [code]443[/code] behind a TLS terminating tunnel.
@export var port: int = 7350

## When [code]true[/code], connects over [code]https[/code] and [code]wss[/code].
@export var use_ssl: bool = false

## Authentication mode used by [method connect_async].
##
## [code]"device"[/code] uses [member device_id]. [code]"custom"[/code] uses
## [member custom_id] and [member auth_vars].
@export_enum("device", "custom") var auth_mode: String = "device"

## Device id used for device authentication. Empty falls back to
## [method OS.get_unique_id]. Set distinct ids per instance for local testing.
@export var device_id: String = ""

## Custom id used when [member auth_mode] is [code]"custom"[/code].
@export var custom_id: String = ""

## Variables sent with Nakama custom authentication.
var auth_vars: Dictionary = { }

## Local Nakama username used for device authentication.
@export var username: String = ""

## Seconds the client waits on each request before timing out.
@export_range(1, 30, 1, "or_greater", "suffix:s") var timeout: int = 3

# Nakama handles, owned by this node.
var _facade
var _client
var _session

# Single-flight guard so concurrent first callers share one authentication.
var _connecting := false
signal _connect_finished(result: Dictionary)


## Returns [code]true[/code] when the Nakama addon scripts are installed.
static func is_addon_present() -> bool:
	return NakamaWrapper.is_addon_present()


## Overrides auth config from [param config] before [method connect_async].
##
## Only present keys are applied.
## [codeblock skip-lint]
## Dictionary
## ├── auth_mode (String)
## ├── server_key (String)
## ├── host (String)
## ├── port (int)
## ├── use_ssl (bool)
## ├── device_id (String)
## ├── custom_id (String)
## ├── auth_vars (Dictionary)
## ├── username (String)
## └── timeout (int)
## [/codeblock]
func configure(config: Dictionary) -> void:
	if config.has("auth_mode"):
		auth_mode = String(config["auth_mode"])
	if config.has("server_key"):
		server_key = String(config["server_key"])
	if config.has("host"):
		host = String(config["host"])
	if config.has("port"):
		port = int(config["port"])
	if config.has("use_ssl"):
		use_ssl = bool(config["use_ssl"])
	if config.has("device_id"):
		device_id = String(config["device_id"])
	if config.has("custom_id"):
		custom_id = String(config["custom_id"])
	if config.has("auth_vars"):
		var vars: Variant = config["auth_vars"]
		auth_vars = vars.duplicate() if typeof(vars) == TYPE_DICTIONARY else { }
	if config.has("username"):
		username = String(config["username"])
	if config.has("timeout"):
		timeout = int(config["timeout"])


## Returns [code]true[/code] once a session has authenticated.
func is_authenticated() -> bool:
	return _client != null and _session != null


## Authenticates the shared Nakama session, reusing an open one.
##
## Concurrent first callers await one authentication.
## [codeblock]
## Returns
## ├── ok (bool)
## └── error (String)
## [/codeblock]
func connect_async() -> Dictionary:
	if not is_addon_present():
		return { "ok": false, "error": "Nakama addon not present" }
	if is_authenticated():
		return { "ok": true, "error": "" }
	if _connecting:
		return await _connect_finished

	_connecting = true
	var result := await _perform_auth()
	_connecting = false
	_connect_finished.emit(result)
	return result


func _perform_auth() -> Dictionary:
	var facade_script: Variant = load(_FACADE_PATH)
	_facade = facade_script.new()
	_facade.name = "NakamaSessionFacade"
	add_child(_facade)

	var scheme := "https" if use_ssl else "http"
	_client = _facade.create_client(
		server_key,
		host,
		port,
		scheme,
		timeout,
		_NAKAMA_LOG_LEVEL_WARNING,
	)

	NakamaWrapper._disable_web_gzip(_client)
	var proxy_base := NakamaWrapper._resolve_proxy_base(self, host)
	NakamaWrapper._apply_proxy_base(_client._api_client, proxy_base, "https")

	var auth_username = username if not username.is_empty() else null
	if auth_mode == "custom":
		_session = await _client.authenticate_custom_async(
			custom_id,
			auth_username,
			true,
			auth_vars,
		)
	else:
		var device := device_id if not device_id.is_empty() else OS.get_unique_id()
		_session = await _client.authenticate_device_async(
			device,
			auth_username,
			true,
		)
	if _session.is_exception():
		return { "ok": false, "error": _session.get_exception().message }
	return { "ok": true, "error": "" }


## Returns the authenticated client, or [code]null[/code] before
## [method connect_async].
func client():
	return _client


## Returns the device session, or [code]null[/code] before
## [method connect_async].
func session():
	return _session


## Returns the authenticated Nakama user id, or an empty string before auth.
func local_user_id() -> String:
	return String(_session.user_id) if _session != null else ""


## Returns the authenticated Nakama username, or an empty string before auth.
func local_username() -> String:
	return String(_session.username) if _session != null else ""


## Calls a Nakama server runtime RPC as the authenticated session user.
##
## A missing RPC returns [code]ok == false[/code]. Callers should fail closed.
## [codeblock]
## Returns
## ├── ok (bool)
## ├── payload (String)
## └── error (String)
## [/codeblock]
func call_rpc_async(rpc_id: String, payload: String = "") -> Dictionary:
	if not is_authenticated():
		return { "ok": false, "payload": "", "error": "session not authenticated" }
	var arg: Variant = payload if not payload.is_empty() else null
	var res = await _client.rpc_async(_session, rpc_id, arg)
	if res.is_exception():
		return { "ok": false, "payload": "", "error": res.get_exception().message }
	return { "ok": true, "payload": String(res.payload), "error": "" }


## Builds a fresh realtime socket from the shared client.
##
## Returns [code]null[/code] before [method connect_async]. The caller owns
## connecting and closing the socket.
func create_socket():
	if _facade == null or _client == null:
		return null
	var socket = _facade.create_socket_from(_client)
	var proxy_base := NakamaWrapper._resolve_proxy_base(self, host)
	NakamaWrapper._apply_proxy_base(socket, proxy_base, "wss")
	return socket


## Registers this node under the [NakamaSessionService] key.
func service_type() -> Script:
	return NakamaSessionService


## Calls [method leave] when the service exits the tree.
func service_exiting(_mt: MultiplayerTree) -> void:
	leave()


## Tears down the facade, client, and session. Idempotent.
func leave() -> void:
	if is_instance_valid(_facade):
		_facade.queue_free()
	_facade = null
	_client = null
	_session = null
