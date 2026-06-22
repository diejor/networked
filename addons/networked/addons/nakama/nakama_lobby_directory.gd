## [LobbyDirectory] backed by a Nakama relay match.
##
## Add as a child of [MultiplayerTree]. Owns a [NakamaWrapper], lazily
## authenticates on the first host or join, and returns the bridge-driven
## [MultiplayerPeer] once the match lifecycle resolves. The relay never opens a
## listening socket, so the host is just whoever claims peer id [code]1[/code].
## That is why a web export can host through this directory.
## [codeblock]
## MultiplayerTree
## └── NakamaLobbyDirectory   (host = relay.example.com, use_ssl = true)
##         owns NakamaWrapper ── socket ── relay match (peer 1 = host)
## [/codeblock]
## Match ids are opaque strings, so discovery is join-by-id. [method list_lobbies]
## reports an empty browse and [method make_join_target] carries the match id as
## the join address.
class_name NakamaLobbyDirectory
extends LobbyDirectory

## Nakama server key, matching the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Relay host name or address, without scheme.
@export var host: String = "127.0.0.1"

## Relay port. Use [code]443[/code] behind a TLS terminating tunnel.
@export var port: int = 7350

## When [code]true[/code], connects over [code]https[/code] and [code]wss[/code].
@export var use_ssl: bool = false

## Device id used for authentication. Empty falls back to
## [method OS.get_unique_id]. Set distinct ids per instance for reliable local
## two-client testing.
@export var device_id: String = ""

## Local Nakama username used for device authentication.
##
## Empty falls back to [method LobbyDirectory.get_local_member_name].
@export var local_member_name: String = ""

## Seconds to wait for a match to fully join before failing.
@export_range(1.0, 30.0, 0.5, "suffix:s") var connect_timeout: float = 10.0

var _wrapper: NakamaWrapper
var _peer: MultiplayerPeer


func service_entered(_mt: MultiplayerTree) -> void:
	_wrapper = NakamaWrapper.new()
	if not NakamaWrapper.is_addon_present():
		Netw.dbg.warn("NakamaLobbyDirectory: Nakama addon not present.")
		provider_unavailable.emit.call_deferred("Nakama addon not present")
		return
	_wrapper.match_join_error.connect(_on_match_join_error)
	_wrapper.socket_closed.connect(_on_socket_closed)


func service_exiting(_mt: MultiplayerTree) -> void:
	if _wrapper != null:
		_wrapper.leave()
	_peer = null


## Implements [method LobbyDirectory.host_lobby] by creating a relay match.
func host_lobby(_server_name: String) -> MultiplayerPeer:
	if not await _ensure_connected():
		return null
	_wrapper.create_match()
	return await _await_match("host_lobby")


## Joins the relay match named [param match_id] and returns its peer.
##
## Nakama match ids are strings, so [NakamaBackend] calls this directly instead
## of the integer [method LobbyDirectory.join_lobby_peer].
func join_match_peer(match_id: String) -> MultiplayerPeer:
	if match_id.is_empty():
		Netw.dbg.warn("NakamaLobbyDirectory: empty match id.")
		return null
	if not await _ensure_connected():
		return null
	_wrapper.join_match(match_id)
	return await _await_match("join_match_peer")


## Match ids are strings, so the integer join path is unsupported. Use
## [method join_match_peer].
func join_lobby_peer(lobby_id: int) -> MultiplayerPeer:
	Netw.dbg.warn(
		"NakamaLobbyDirectory: join_lobby_peer(%d) unsupported; " +
		"use join_match_peer with the string match id.",
		[lobby_id],
	)
	return null


## Relay matches are not server-listable, so browsing returns empty.
func list_lobbies() -> void:
	lobby_list_updated.emit([] as Array[LobbyDirectory.LobbyInfo])


## Implements [method LobbyDirectory.leave_lobby]. Idempotent.
func leave_lobby() -> void:
	if _wrapper != null:
		_wrapper.leave()
	_peer = null


## Stamps a [JoinTarget] whose address is the relay match id.
func make_join_target(lobby: LobbyDirectory.LobbyInfo) -> JoinTarget:
	var target := JoinTarget.new()
	target.display_name = lobby.lobby_name
	target.address = String(lobby.metadata.get("match_id", str(lobby.id)))
	target.metadata = lobby.metadata.duplicate()
	target.backend = NakamaBackend.new()
	return target


## Returns the active relay match id for [method MultiplayerTree.join].
func get_join_address() -> String:
	return _wrapper.match_id() if _wrapper != null else ""


## Resolves [param peer_id] to its Nakama username when known.
func get_member_name(peer_id: int) -> String:
	if _wrapper == null:
		return super.get_member_name(peer_id)
	var name := _wrapper.username_for_peer(peer_id)
	return name if not name.is_empty() else super.get_member_name(peer_id)


## Returns [member local_member_name] when configured.
func get_local_member_name() -> String:
	if not local_member_name.is_empty():
		return local_member_name
	return super.get_local_member_name()


func _ensure_connected() -> bool:
	if _wrapper == null or not NakamaWrapper.is_addon_present():
		return false
	if _wrapper.is_ready():
		return true
	var res := await _wrapper.connect_async(self, {
		"server_key": server_key,
		"host": host,
		"port": port,
		"use_ssl": use_ssl,
		"device_id": device_id,
		"username": get_local_member_name(),
	})
	if not res.ok:
		Netw.dbg.error("NakamaLobbyDirectory: connect failed: %s", [res.error])
		provider_unavailable.emit(String(res.error))
		return false
	return true


func _await_match(op: String) -> MultiplayerPeer:
	var timer := get_tree().create_timer(connect_timeout)
	var outcome := await Async.timeout_or_failure(
		_wrapper.match_joined,
		_wrapper.match_join_error,
		timer,
	)
	if outcome.result != "success":
		Netw.dbg.error(
			"NakamaLobbyDirectory: %s did not join (%s).",
			[op, outcome.result],
		)
		_wrapper.leave()
		return null
	_peer = _wrapper.peer()
	Netw.dbg.info("NakamaLobbyDirectory: %s joined match %s.", [op, _wrapper.match_id()])
	return _peer


func _on_match_join_error(message: String) -> void:
	Netw.dbg.warn("NakamaLobbyDirectory: match join error: %s", [message])


func _on_socket_closed() -> void:
	Netw.dbg.info("NakamaLobbyDirectory: socket closed.")
	_peer = null
