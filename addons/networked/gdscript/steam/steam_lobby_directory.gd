## A [LobbyDirectory] that hosts, joins and lists Steam lobbies.
##
## Add it under a [MultiplayerTree] to use Steam lobbies with
## [code]SteamMultiplayerPeer[/code]. A lobby's address is its Steam lobby id.
##
## Steam allows one instance per process, so a second one frees itself.
class_name SteamLobbyDirectory
extends LobbyDirectory

const Async := preload("res://addons/networked/gdscript/async.gd")

const STEAM_APP_ID_SETTING := "steam/initialization/app_id"
const SPACEWAR_APP_ID := 480

static var _instance: WeakRef = weakref(null)

## The player limit, when the host sets none.
@export_range(1, 250, 1, "or_greater", "suffix:players") \
		var max_clients: int = 8

## Only lobbies with the same tag are listed, so games do not see each other's
## lobbies.
@export var browser_filter_uid: String = "networked"

## Disables Nagle's algorithm on the peer.
@export var disable_nagle: bool = true

## Lets Steam relay traffic when a direct connection fails.
@export var allow_p2p_relay: bool = true

## Uses the Spacewar test app when [code]steam/initialization/app_id[/code]
## is not set. The project settings are not changed.
@export var allow_spacewar_fallback: bool = false

## Hides lobbies hosted by your own Steam account, since one account cannot
## reliably join itself.
@export var reject_own_lobbies: bool = true

var _wrapper: SteamWrapper
var _lobby_id: int = 0
var _peer: MultiplayerPeer
var _pending_list: bool = false
var _pending_create_name: String = ""
var _pending_lobby_type: SteamWrapper.LobbyType = SteamWrapper.LobbyType.PUBLIC
var _pending_visibility: NetwServerInfo.Visibility = \
		NetwServerInfo.VISIBILITY_PUBLIC
var _pending_max: int = 0
var _pending_join_lobby_id: int = 0
var _init_ok: bool = false
var _joining: bool = false

## Emitted when Steam reports a connection failure during [method _join_lobby].
signal peer_connect_failed(reason: String)
signal _lobby_created_internal(peer: MultiplayerPeer)
signal _lobby_joined_internal(peer: MultiplayerPeer)


func _should_register() -> bool:
	if NetwService.is_transport_restricted():
		return false
	return SteamWrapper.is_running()


# Steam allows one initialization per process.
func _service_entered(_api: NetwMultiplayer) -> void:
	var existing: SteamLobbyDirectory = _instance.get_ref()
	if existing and existing != self:
		push_warning(
			"SteamLobbyDirectory: only one instance is allowed. " +
			"Queueing duplicate for deletion.",
		)
		Netw.service_unregister(self, self)
		queue_free()
		return
	_instance = weakref(self)

	_wrapper = SteamWrapper.new()
	if not _wrapper.is_available():
		_init_ok = false
		push_warning("SteamLobbyDirectory: GodotSteam singleton not found.")
		provider_unavailable.emit.call_deferred(
			"GodotSteam singleton not found",
		)
		Netw.service_unregister(self, self)
		return

	if not _has_steam_app_id():
		if allow_spacewar_fallback:
			_apply_spacewar_fallback()
		else:
			var reason := _steam_app_id_required_message()
			assert(false, "SteamLobbyDirectory: %s" % reason)
			_init_ok = false
			push_error("SteamLobbyDirectory: %s" % reason)
			provider_unavailable.emit.call_deferred(reason)
			Netw.service_unregister(self, self)
			return

	var init_res: Dictionary = _wrapper.steam_init_ex()
	var status: int = init_res.get(
		"status",
		SteamWrapper.InitResult.FAILED_GENERIC,
	)
	_init_ok = status == SteamWrapper.InitResult.OK
	if not _init_ok:
		var status_str := SteamWrapper.init_result_to_string(status)
		var verbal: String = init_res.get("verbal", "")
		var reason := "Steam init failed (status %s)" % status_str
		if not verbal.is_empty():
			reason += ": " + verbal
		else:
			reason += ": " + SteamWrapper.init_result_to_reason(status)
		push_warning("SteamLobbyDirectory: %s" % reason)
		provider_unavailable.emit.call_deferred(reason)
		Netw.service_unregister(self, self)
		return

	_wrapper.connect_signal("lobby_created", _on_lobby_created)
	_wrapper.connect_signal("lobby_joined", _on_lobby_joined)
	_wrapper.connect_signal("lobby_match_list", _on_lobby_match_list)
	_wrapper.connect_signal("join_requested", _on_join_requested)
	_wrapper.connect_signal("p2p_session_connect_fail", _on_p2p_connect_fail)
	_wrapper.connect_signal("network_connection_status_changed", _on_network_connection_status_changed)

	var mt := Netw.session(self).root as MultiplayerTree
	if mt:
		_bind_tree_signals(mt)


func _service_exiting(_api: NetwMultiplayer) -> void:
	var existing: SteamLobbyDirectory = _instance.get_ref()
	if existing == self:
		_instance = weakref(null)

	if _wrapper and _wrapper.is_available():
		_wrapper.disconnect_signal("lobby_created", _on_lobby_created)
		_wrapper.disconnect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.disconnect_signal("lobby_match_list", _on_lobby_match_list)
		_wrapper.disconnect_signal("join_requested", _on_join_requested)
		_wrapper.disconnect_signal("p2p_session_connect_fail", _on_p2p_connect_fail)
		_wrapper.disconnect_signal(
			"network_connection_status_changed",
			_on_network_connection_status_changed,
		)

	if _lobby_id != 0 and _wrapper:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0


func _process(_dt: float) -> void:
	if _init_ok and _wrapper:
		_wrapper.run_callbacks()


## Returns [code]true[/code] if Steam initialized successfully.
func is_ready() -> bool:
	return _init_ok


func _capabilities() -> LobbyDirectory.Capability:
	return (
			LobbyDirectory.CAPABILITY_BROWSE
			| LobbyDirectory.CAPABILITY_FRIENDS_ONLY_SUPPORT
			| LobbyDirectory.CAPABILITY_INVITES
			| LobbyDirectory.CAPABILITY_FRIEND_NAMES
	)


func _map_visibility(v: NetwServerInfo.Visibility) -> SteamWrapper.LobbyType:
	match v:
		NetwServerInfo.VISIBILITY_FRIENDS_ONLY:
			return SteamWrapper.LobbyType.FRIENDS_ONLY
		NetwServerInfo.VISIBILITY_PRIVATE:
			return SteamWrapper.LobbyType.PRIVATE
		_:
			return SteamWrapper.LobbyType.PUBLIC


func _parse_visibility(raw: String) -> NetwServerInfo.Visibility:
	if raw.is_empty():
		return NetwServerInfo.VISIBILITY_PUBLIC
	return int(raw) as NetwServerInfo.Visibility


## Returns the active lobby ID, or [code]0[/code] when no lobby is joined.
func get_lobby_id() -> int:
	return _lobby_id


## Returns the local user's display name.
func get_persona_name() -> String:
	return _wrapper.get_persona_name() if _init_ok else ""


func _member_name(peer_id: int) -> String:
	if not _init_ok or _peer == null:
		return LobbyDirectory.member_name_default(peer_id)
	var steam_id := _wrapper.get_steam_id_from_peer_id(_peer, peer_id)
	if steam_id == 0:
		return LobbyDirectory.member_name_default(peer_id)
	var persona := _wrapper.get_friend_persona_name(steam_id)
	return persona if not persona.is_empty() else LobbyDirectory.member_name_default(peer_id)


func _local_member_name() -> String:
	var persona := get_persona_name()
	if not persona.is_empty():
		return persona
	return LobbyDirectory.local_member_name_default()


func _display_name() -> String:
	return "Steam"


func _is_available() -> bool:
	return not OS.has_feature("web")


func _address_label() -> String:
	return "Lobby ID"


func _address_help() -> String:
	return "Steam lobby IDs are discovered through the server browser."


func _list_lobbies() -> void:
	if not _guard_ready("_list_lobbies"):
		publish_lobbies(
			PackedStringArray(),
			PackedStringArray(),
			[] as Array[NetwServerInfo],
		)
		return
	_pending_list = true
	if not browser_filter_uid.is_empty():
		_wrapper.add_request_lobby_list_string_filter(
			"uid",
			browser_filter_uid,
			SteamWrapper.LobbyComparison.EQUAL,
		)
	_wrapper.add_request_lobby_list_distance_filter(
		SteamWrapper.LobbyDistance.WORLDWIDE,
	)
	_wrapper.request_lobby_list()


func _leave_lobby() -> void:
	_joining = false
	_pending_join_lobby_id = 0
	if _lobby_id == 0 or not _wrapper:
		return
	_wrapper.leave_lobby(_lobby_id)
	_lobby_id = 0
	_peer = null


func _peer_class() -> StringName:
	return &"SteamMultiplayerPeer"


func _join_address() -> String:
	return str(_lobby_id) if _lobby_id != 0 else ""


func _host_lobby(settings: Dictionary) -> void:
	if not _guard_ready("_host_lobby"):
		fail(ERR_UNAVAILABLE, "Steam is unavailable.")
		return
	if _lobby_id != 0:
		fail(ERR_ALREADY_IN_USE, "already in Steam lobby %d." % _lobby_id)
		return
	_pending_create_name = String(settings.get("name", ""))
	_pending_visibility = int(
		settings.get(
			"visibility",
			NetwServerInfo.VISIBILITY_PUBLIC,
		),
	) as NetwServerInfo.Visibility
	_pending_lobby_type = _map_visibility(_pending_visibility)
	var wanted := int(settings.get("max_players", 0))
	_pending_max = wanted if wanted > 0 else max_clients
	_wrapper.create_lobby(int(_pending_lobby_type), _pending_max)

	var timer := get_tree().create_timer(10.0)
	var timed_out := await Async.timeout(_lobby_created_internal, timer)
	if timed_out:
		fail(ERR_TIMEOUT, "creating the Steam lobby timed out.")
		return
	if _peer == null:
		fail(ERR_CANT_CREATE, "Steam produced no lobby peer.")
		return
	deliver(_peer)


func _join_lobby(address: String) -> void:
	if not _guard_ready("_join_lobby"):
		fail(ERR_UNAVAILABLE, "Steam is unavailable.")
		return
	var lobby_id := int(address)
	if lobby_id <= 0:
		fail(ERR_INVALID_PARAMETER, "invalid lobby id '%s'." % address)
		return
	if reject_own_lobbies and _is_own_lobby(lobby_id):
		fail(ERR_UNAUTHORIZED, "refusing to join own lobby %d." % lobby_id)
		return
	if _pending_join_lobby_id != 0:
		fail(ERR_BUSY, "lobby %d is still pending." % _pending_join_lobby_id)
		return
	if _lobby_id != 0:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0
	_joining = true
	_pending_join_lobby_id = lobby_id
	_wrapper.join_lobby(lobby_id)

	var timer := get_tree().create_timer(10.0)
	var outcome := await Async.timeout_or_failure(
		_lobby_joined_internal,
		peer_connect_failed,
		timer,
	)
	_joining = false
	if outcome.result == "timeout":
		fail(ERR_TIMEOUT, "joining the Steam lobby timed out.")
		return
	if outcome.result == "failure":
		fail(ERR_CANT_CONNECT, String(outcome.reason))
		return
	if _peer == null:
		fail(ERR_CANT_CONNECT, "Steam produced no lobby peer.")
		return
	deliver(_peer)


func _bind_tree_signals(mt: MultiplayerTree) -> void:
	if not mt.api.peer_connected.is_connected(_on_tree_peer_changed):
		mt.api.peer_connected.connect(_on_tree_peer_changed)
	if not mt.api.peer_disconnected.is_connected(_on_tree_peer_changed):
		mt.api.peer_disconnected.connect(_on_tree_peer_changed)
	var session: NetwSessionHandle = Netw.session(mt)
	if not session.disconnecting.is_connected(_on_tree_server_disconnecting):
		session.disconnecting.connect(_on_tree_server_disconnecting)


func _on_tree_peer_changed(_peer_id: int) -> void:
	if _peer_id == 1:
		_joining = false
	if _lobby_id == 0 or not _wrapper:
		return
	var count: int = _wrapper.get_num_lobby_members(_lobby_id)
	_wrapper.set_lobby_data(_lobby_id, "players", str(count))


func _on_tree_server_disconnecting(_reason: String) -> void:
	_leave_lobby()


func _guard_ready(op: String) -> bool:
	if not _init_ok:
		push_warning(
			"SteamLobbyDirectory: %s called while Steam is unavailable." % [op],
		)
		return false
	return true


func _has_steam_app_id() -> bool:
	if not ProjectSettings.has_setting(STEAM_APP_ID_SETTING):
		return false
	var raw: Variant = ProjectSettings.get_setting(STEAM_APP_ID_SETTING)
	var app_id := str(raw).strip_edges()
	return not app_id.is_empty() and app_id != "0"


func _apply_spacewar_fallback() -> void:
	ProjectSettings.set_setting(STEAM_APP_ID_SETTING, SPACEWAR_APP_ID)
	var reason := _steam_app_id_fallback_message()
	push_warning("SteamLobbyDirectory: %s" % reason)


func _steam_app_id_required_message() -> String:
	return (
			"Project setting `%s` must not be empty or 0. Set it to " +
			"your Steam app id, or use 480 for Spacewar while testing " +
			"before you have one."
	) % STEAM_APP_ID_SETTING


func _steam_app_id_fallback_message() -> String:
	return (
			"Project setting `%s` is empty or 0. Using 480 (Spacewar) " +
			"because `allow_spacewar_fallback` is enabled. Set this " +
			"project setting to your Steam app id before publishing."
	) % STEAM_APP_ID_SETTING


func _on_lobby_created(connect_result: int, lobby_id: int) -> void:
	if connect_result != 1:
		var reason := "Lobby create failed (code %d)" % connect_result
		push_error("SteamLobbyDirectory: %s" % [reason])
		_pending_create_name = ""
		_lobby_created_internal.emit(null)
		return

	_lobby_id = lobby_id
	var lobby_name := _pending_create_name
	_pending_create_name = ""

	_wrapper.set_lobby_joinable(lobby_id, true)
	_wrapper.allow_p2p_packet_relay(allow_p2p_relay)
	if not lobby_name.is_empty():
		_wrapper.set_lobby_data(lobby_id, "name", lobby_name)
	if not browser_filter_uid.is_empty():
		_wrapper.set_lobby_data(lobby_id, "uid", browser_filter_uid)
	_wrapper.set_lobby_data(lobby_id, "app_id", _local_app_id())
	_wrapper.set_lobby_data(lobby_id, "host", _wrapper.get_persona_name())
	_wrapper.set_lobby_data(lobby_id, "players", "1")
	_wrapper.set_lobby_data(lobby_id, "max", str(_pending_max))
	_wrapper.set_lobby_data(lobby_id, "visibility", str(int(_pending_visibility)))

	var peer := _build_peer()
	if peer == null:
		_lobby_created_internal.emit(null)
		return
	var err: Error = peer.call(&"host_with_lobby", lobby_id)
	if err != OK:
		push_error(
			"SteamLobbyDirectory: host_with_lobby failed: %s"
			% [error_string(err)],
		)
		_lobby_created_internal.emit(null)
		return

	_peer = peer
	_lobby_created_internal.emit(peer)


func _on_lobby_joined(
		lobby_id: int,
		_permissions: int,
		_locked: bool,
		response: int,
) -> void:
	if response != 1:
		var reason := SteamWrapper.chat_room_enter_response_to_string(response)
		push_error("SteamLobbyDirectory: join failed: %s" % [reason])
		_pending_join_lobby_id = 0
		_lobby_joined_internal.emit(null)
		return

	# Steam also calls this on the host for its own lobby.
	if _peer != null and _lobby_id == lobby_id:
		_pending_join_lobby_id = 0
		return

	_lobby_id = lobby_id
	var peer := _build_peer()
	if peer == null:
		_pending_join_lobby_id = 0
		_lobby_id = 0
		_wrapper.leave_lobby(lobby_id)
		_lobby_joined_internal.emit(null)
		return
	var err: Error = peer.call(&"connect_to_lobby", lobby_id)
	if err != OK:
		push_error(
			"SteamLobbyDirectory: connect_to_lobby failed: %s"
			% [error_string(err)],
		)
		_pending_join_lobby_id = 0
		_lobby_id = 0
		_wrapper.leave_lobby(lobby_id)
		_lobby_joined_internal.emit(null)
		return

	_peer = peer
	_pending_join_lobby_id = 0
	_lobby_joined_internal.emit(peer)


func _on_lobby_match_list(lobbies: Array) -> void:
	if not _pending_list:
		return
	_pending_list = false

	var addresses := PackedStringArray()
	var names := PackedStringArray()
	var infos: Array[NetwServerInfo] = []
	for raw_id in lobbies:
		var id := int(raw_id)
		if reject_own_lobbies and _is_own_lobby(id):
			push_warning(
				("SteamLobbyDirectory: ignoring own lobby %d in browse " +
						"results. Steam local testing requires a second account.") % id,
			)
			continue

		var uid := _wrapper.get_lobby_data(id, "uid")
		if (
				not browser_filter_uid.is_empty()
				and uid != browser_filter_uid
		):
			continue
		var info := NetwServerInfo.new()
		info.app_id = StringName(_wrapper.get_lobby_data(id, "app_id"))
		info.players = _wrapper.get_num_lobby_members(id)
		info.max_players = _wrapper.get_lobby_member_limit(id)
		info.visibility = _parse_visibility(
			_wrapper.get_lobby_data(id, "visibility"),
		)
		info.metadata = {
			"host": _wrapper.get_lobby_data(id, "host"),
			"uid": uid,
		}
		addresses.append(str(id))
		names.append(_wrapper.get_lobby_data(id, "name"))
		infos.append(info)
	publish_lobbies(addresses, names, infos)


func _on_join_requested(lobby_id: int, friend_id: int) -> void:
	invite_received.emit(lobby_id, friend_id)


func _build_peer() -> MultiplayerPeer:
	var peer := _wrapper.create_peer()
	if peer == null:
		push_error(
			"SteamLobbyDirectory: SteamMultiplayerPeer class not available.",
		)
		return null
	_wrapper.configure_peer(peer, not disable_nagle, allow_p2p_relay)
	return peer


func _local_app_id() -> String:
	var session: NetwSessionHandle = Netw.session(self)
	return String(session.config.app_id) if session else ""


func _is_own_lobby(lobby_id: int) -> bool:
	if _wrapper == null:
		return false
	var local_id := _wrapper.get_steam_id()
	return local_id != 0 and _wrapper.get_lobby_owner(lobby_id) == local_id


func _on_p2p_connect_fail(steam_id: int, error: int) -> void:
	if _joining:
		push_warning(
			"SteamLobbyDirectory: P2P connect failed to %d (error %d)"
			% [steam_id, error],
		)
		_joining = false
		peer_connect_failed.emit("P2P session connect fail")


func _on_network_connection_status_changed(info: Dictionary) -> void:
	if not _joining:
		return
	var state: int = info.get("connection_state", -1)
	if state == 4 or state == 5: # ClosedByPeer or ProblemDetectedLocally
		push_warning(
			"SteamLobbyDirectory: connection status failed (%d)" % [state],
		)
		_joining = false
		peer_connect_failed.emit("Network connection status failed")
