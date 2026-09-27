## A [LobbyDirectory] that hosts and joins Nakama relay matches.
##
## The host needs no open port, so web exports can host. A lobby's address is
## its match id, which players can share to join directly. Lobby names and
## settings are stored in Nakama storage so they can be listed.
class_name NakamaLobbyDirectory
extends LobbyDirectory

const Async := preload("res://addons/networked/gdscript/async.gd")

## Nakama server key, matching the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Relay host name or address, without scheme.
@export var host: String = "127.0.0.1"

## Relay port. Use [code]443[/code] behind a TLS terminating tunnel.
@export var port: int = 7350

## When [code]true[/code], connects over [code]https[/code] and [code]wss[/code].
@export var use_ssl: bool = false

## Device id used for authentication. Empty uses [method OS.get_unique_id].
@export var device_id: String = ""

## The Nakama username. Empty uses [method LobbyDirectory._local_member_name].
@export var local_member_name: String = ""

## In debug desktop builds, adds a suffix to [member device_id] and
## [member local_member_name] so several instances on one machine log in as
## different users. Set the suffix with [code]--nakama-instance=name[/code],
## [code]--netw-instance=name[/code], [code]--instance=name[/code] or
## [code]NAKAMA_INSTANCE_SUFFIX[/code].
@export var uniquify_debug_identity: bool = true

## Seconds to wait for a match to fully join before failing.
@export_range(1.0, 30.0, 0.5, "suffix:s") var connect_timeout: float = 10.0

## The player limit shown in the lobby list, when the host sets none.
@export_range(1, 250, 1, "or_greater", "suffix:players") var max_clients: int = 8

## Only lobbies with the same tag are listed, so games sharing a Nakama server
## do not see each other's lobbies.
@export var browser_filter_uid: String = "networked"


## The listing of one lobby, as stored in Nakama.
class LobbyCard:
	extends Resource

	## Relay match id this card describes. Carried as the storage key, not in
	## the serialized body.
	@export var match_id: String = ""

	## Advertised lobby name.
	@export var lobby_name: String = ""

	## Host display name.
	@export var host: String = ""

	## The host's [member MultiplayerTree.app_id].
	@export var app_id: String = ""

	## Game tag, matching [member NakamaLobbyDirectory.browser_filter_uid].
	@export var uid: String = ""

	## Maximum member count.
	@export var max_players: int = 0

	## Advertised [enum NetwServerInfo.Visibility].
	@export var visibility: NetwServerInfo.Visibility = \
			NetwServerInfo.VISIBILITY_PUBLIC


	## Returns the listing as a [Dictionary] to store.
	func to_dict() -> Dictionary:
		return {
			"name": lobby_name,
			"host": host,
			"app_id": app_id,
			"uid": uid,
			"max": max_players,
			"visibility": int(visibility),
		}


	## Creates a listing from a stored [Dictionary].
	static func from_dict(match_id: String, data: Dictionary) -> LobbyCard:
		var card := LobbyCard.new()
		card.match_id = match_id
		card.lobby_name = String(data.get("name", ""))
		card.host = String(data.get("host", ""))
		card.app_id = String(data.get("app_id", ""))
		card.uid = String(data.get("uid", ""))
		card.max_players = int(data.get("max", 0))
		card.visibility = (
				int(data.get("visibility", NetwServerInfo.VISIBILITY_PUBLIC)) as NetwServerInfo.Visibility
		)
		return card


	## Returns the listing as a [NetwServerInfo] with [param players] players.
	func to_server_info(players: int) -> NetwServerInfo:
		var info := NetwServerInfo.new()
		info.app_id = StringName(app_id)
		info.players = players
		info.max_players = max_players
		info.visibility = visibility
		info.metadata = { "host": host, "uid": uid }
		return info


var _wrapper: NakamaWrapper
var _peer: MultiplayerPeer
var _hosted_match_id: String = ""
var _session_bound: bool = false


func _service_entered(_api: NetwMultiplayer) -> void:
	_wrapper = NakamaWrapper.new()
	if not NakamaWrapper.is_addon_present():
		push_warning("NakamaLobbyDirectory: Nakama addon not present.")
		provider_unavailable.emit.call_deferred("Nakama addon not present")
		return
	_wrapper.match_join_error.connect(_on_match_join_error)
	_wrapper.socket_closed.connect(_on_socket_closed)


func _service_exiting(_api: NetwMultiplayer) -> void:
	if _wrapper != null:
		if not _hosted_match_id.is_empty():
			_wrapper.delete_lobby_card(_hosted_match_id)
			_hosted_match_id = ""
		_wrapper.leave()
	_peer = null


## A [constant NetwServerInfo.VISIBILITY_PRIVATE] lobby is not listed, and can
## only be joined by its match id.
func _host_lobby(settings: Dictionary) -> void:
	if not await _ensure_connected():
		fail(ERR_UNAVAILABLE, "the Nakama relay is unreachable.")
		return
	_wrapper.create_match()
	var peer := await _await_match("_host_lobby")
	if peer == null:
		fail(ERR_CANT_CREATE, "the relay produced no match peer.")
		return
	await _publish_card(settings)
	deliver(peer)


func _publish_card(settings: Dictionary) -> void:
	var visibility := int(
		settings.get(
			"visibility",
			NetwServerInfo.VISIBILITY_PUBLIC,
		),
	) as NetwServerInfo.Visibility
	if visibility == NetwServerInfo.VISIBILITY_FRIENDS_ONLY:
		push_warning(
			"NakamaLobbyDirectory: FRIENDS_ONLY unsupported, hosting PRIVATE.",
		)
		visibility = NetwServerInfo.VISIBILITY_PRIVATE
	if visibility == NetwServerInfo.VISIBILITY_PRIVATE:
		return
	var mid := _wrapper.match_id()
	if mid.is_empty():
		return
	var card := LobbyCard.new()
	card.match_id = mid
	card.lobby_name = String(settings.get("name", ""))
	card.host = _local_member_name()
	card.app_id = _local_app_id()
	card.uid = browser_filter_uid
	var wanted := int(settings.get("max_players", 0))
	card.max_players = wanted if wanted > 0 else max_clients
	card.visibility = visibility
	_hosted_match_id = mid
	var ok := await _wrapper.write_lobby_card(mid, card.to_dict())
	if not ok:
		push_warning("NakamaLobbyDirectory: failed to publish lobby card.")
		_hosted_match_id = ""


func _join_lobby(address: String) -> void:
	if address.is_empty():
		fail(ERR_INVALID_PARAMETER, "the match id is empty.")
		return
	if not await _ensure_connected():
		fail(ERR_UNAVAILABLE, "the Nakama relay is unreachable.")
		return
	_wrapper.join_match(address)
	var peer := await _await_match("_join_lobby")
	if peer == null:
		fail(ERR_CANT_CONNECT, "the relay produced no match peer.")
		return
	deliver(peer)


func _list_lobbies() -> void:
	var addresses := PackedStringArray()
	var names := PackedStringArray()
	var infos: Array[NetwServerInfo] = []
	if not await _ensure_connected():
		publish_lobbies(addresses, names, infos)
		return
	var cards := await _wrapper.read_lobby_cards()
	var matches := await _wrapper.list_matches()
	var live_sizes: Dictionary = { }
	for m in matches:
		live_sizes[String(m.match_id)] = int(m.size)

	for mid in cards:
		if not live_sizes.has(mid):
			continue
		var card := LobbyCard.from_dict(mid, cards[mid])
		if card.uid != browser_filter_uid:
			continue
		addresses.append(mid)
		names.append(card.lobby_name)
		infos.append(card.to_server_info(int(live_sizes[mid])))
	publish_lobbies(addresses, names, infos)


func _capabilities() -> LobbyDirectory.Capability:
	return (
			LobbyDirectory.CAPABILITY_BROWSE
			| LobbyDirectory.CAPABILITY_FRIEND_NAMES
	)


func _leave_lobby() -> void:
	if _wrapper != null:
		if not _hosted_match_id.is_empty():
			await _wrapper.delete_lobby_card(_hosted_match_id)
			_hosted_match_id = ""
		_wrapper.leave()
	_peer = null


func _peer_class() -> StringName:
	return &"NakamaRelayPeer"


func _display_name() -> String:
	return "Nakama"


func _is_available() -> bool:
	return NakamaWrapper.is_addon_present()


func _can_host_here() -> bool:
	return NakamaWrapper.is_addon_present()


func _address_label() -> String:
	return "Match ID"


func _address_help() -> String:
	return "Paste the Nakama match id shared by the host."


func _join_address() -> String:
	return _wrapper.match_id() if _wrapper != null else ""


## Returns the [NakamaWrapper] in use.
func wrapper() -> NakamaWrapper:
	return _wrapper


func _member_name(peer_id: int) -> String:
	if _wrapper == null:
		return LobbyDirectory.member_name_default(peer_id)
	var name := _wrapper.username_for_peer(peer_id)
	return name if not name.is_empty() else LobbyDirectory.member_name_default(peer_id)


func _local_member_name() -> String:
	return _effective_local_member_name()


func _local_app_id() -> String:
	var session: NetwSessionHandle = Netw.session(self)
	return String(session.config.app_id) if session else ""


func _ensure_connected() -> bool:
	if _wrapper == null or not NakamaWrapper.is_addon_present():
		return false
	if _wrapper.is_ready():
		return true
	if not _session_bound:
		_session_bound = true
		var session := NakamaSessionService.of(self)
		if session != null:
			_wrapper.use_session(session)
	var res := await _wrapper.connect_async(
		self,
		{
			"server_key": server_key,
			"host": host,
			"port": port,
			"use_ssl": use_ssl,
			"device_id": _effective_device_id(),
			"username": _effective_local_member_name(),
		},
	)
	if not res.ok:
		var details := "connect to %s:%d failed: %s" % [host, port, res.error]
		push_warning("NakamaLobbyDirectory: %s" % details)
		provider_unavailable.emit(details)
		return false
	return true


func _effective_device_id() -> String:
	return _with_debug_instance_suffix(device_id)


func _effective_local_member_name() -> String:
	var name := local_member_name
	if name.is_empty():
		name = LobbyDirectory.local_member_name_default()
	return _with_debug_instance_suffix(name)


func _with_debug_instance_suffix(base: String) -> String:
	if not _should_uniquify_debug_identity():
		return base
	if base.is_empty():
		base = "nakama-debug"
	return (base + "-" + str(OS.get_process_id())).left(128)


func _should_uniquify_debug_identity() -> bool:
	return (
			uniquify_debug_identity
			and OS.has_feature("debug")
			and not (OS.has_feature("web") or OS.get_name() == "Web")
	)


func _await_match(op: String) -> MultiplayerPeer:
	var timer := get_tree().create_timer(connect_timeout)
	var outcome := await Async.timeout_or_failure(
		_wrapper.match_joined,
		_wrapper.match_join_error,
		timer,
	)
	if outcome.result != "success":
		push_error(
			"NakamaLobbyDirectory: %s did not join (%s)."
			% [op, outcome.result],
		)
		_wrapper.leave()
		return null
	_peer = _wrapper.peer()
	return _peer


func _on_match_join_error(message: String) -> void:
	push_warning("NakamaLobbyDirectory: match join error: %s" % [message])


func _on_socket_closed() -> void:
	_peer = null
