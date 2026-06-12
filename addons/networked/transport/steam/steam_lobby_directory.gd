## Steam-backed [LobbyDirectory] implementation.
##
## Add as a child of [MultiplayerTree]. Owns the [SteamWrapper], drives Steam
## callbacks each frame, and translates Steam signals into directory signals.
## [br][br]
## Only one instance may exist per process; duplicates queue themselves for
## deletion. [member browser_filter_uid] tags hosted lobbies so the browser
## only returns lobbies created with the same game id.
class_name SteamLobbyDirectory
extends LobbyDirectory

const STEAM_APP_ID_SETTING := "steam/initialization/app_id"
const SPACEWAR_APP_ID := 480

static var _instance: WeakRef = weakref(null)

## Maximum number of simultaneous lobby members.
@export_range(1, 250, 1, "or_greater", "suffix:players") \
		var max_clients: int = 8

## Default visibility used by [method host_lobby].
@export var default_lobby_type: SteamWrapper.LobbyType = \
		SteamWrapper.LobbyType.PUBLIC

## Tag stored under the [code]uid[/code] lobby key. Browser filters on this so
## different games don't pollute each other's lobby lists.
@export var browser_filter_uid: String = "networked"

## If [code]true[/code], disables Nagle's algorithm on the produced peer.
@export var disable_nagle: bool = true

## If [code]true[/code], allows Steam to relay traffic when direct P2P fails.
@export var allow_p2p_relay: bool = true

## If [code]true[/code], uses Spacewar when
## [code]steam/initialization/app_id[/code] is missing, empty, or
## [code]0[/code]. This runtime fallback does not save project settings.
@export var allow_spacewar_fallback: bool = false

## When [code]true[/code], lobbies owned by the local Steam account are
## hidden and rejected. Steam does not support testing two local peers
## through one account reliably.
@export var reject_own_lobbies: bool = true

var _wrapper: SteamWrapper
var _lobby_id: int = 0
var _peer: MultiplayerPeer
var _pending_list: bool = false
var _pending_create_name: String = ""
var _pending_join_lobby_id: int = 0
var _init_ok: bool = false
var _joining: bool = false

signal peer_connect_failed(reason: String)
signal _lobby_created_internal(peer: MultiplayerPeer)
signal _lobby_joined_internal(peer: MultiplayerPeer)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	if Netw.is_test_env():
		return

	var existing: SteamLobbyDirectory = _instance.get_ref()
	if existing and existing != self:
		push_error(
			"SteamLobbyDirectory: only one instance is allowed. " +
			"Queueing duplicate for deletion.",
		)
		queue_free()
		return
	_instance = weakref(self)

	_wrapper = SteamWrapper.new()
	if not _wrapper.is_available():
		_init_ok = false
		Netw.dbg.warn(
			"SteamLobbyDirectory: GodotSteam singleton not found.",
		)
		provider_unavailable.emit.call_deferred(
			"GodotSteam singleton not found",
		)
		return

	if not _has_steam_app_id():
		if allow_spacewar_fallback:
			_apply_spacewar_fallback()
		else:
			var reason := _steam_app_id_required_message()
			assert(false, "SteamLobbyDirectory: %s" % reason)
			_init_ok = false
			Netw.dbg.error(
				"SteamLobbyDirectory: %s",
				[reason],
				func(m): push_error(m)
			)
			provider_unavailable.emit.call_deferred(reason)
			return

	var init_res: Dictionary = _wrapper.steam_init_ex()
	var status: int = init_res.get(
		"status",
		SteamWrapper.InitResult.FAILED_GENERIC,
	)
	_init_ok = status == SteamWrapper.InitResult.OK
	if not _init_ok:
		var reason := "Steam init failed (status %d)" % status
		if status == SteamWrapper.InitResult.NO_STEAM_CLIENT:
			reason += ": no Steam client running"
		Netw.dbg.error(
			"SteamLobbyDirectory: %s",
			[reason],
			func(m): push_error(m)
		)
		provider_unavailable.emit.call_deferred(reason)
		return

	_wrapper.connect_signal("lobby_created", _on_lobby_created)
	_wrapper.connect_signal("lobby_joined", _on_lobby_joined)
	_wrapper.connect_signal("lobby_match_list", _on_lobby_match_list)
	_wrapper.connect_signal("join_requested", _on_join_requested)
	_wrapper.connect_signal("p2p_session_connect_fail", _on_p2p_connect_fail)
	_wrapper.connect_signal("network_connection_status_changed", _on_network_connection_status_changed)

	NetwServices.register(self)

	var mt := MultiplayerTree.resolve(self)
	if mt:
		_bind_tree_signals(mt)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	var existing: SteamLobbyDirectory = _instance.get_ref()
	if existing == self:
		_instance = weakref(null)

	if _wrapper and _wrapper.is_available():
		_wrapper.disconnect_signal("lobby_created", _on_lobby_created)
		_wrapper.disconnect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.disconnect_signal("lobby_match_list", _on_lobby_match_list)
		_wrapper.disconnect_signal("join_requested", _on_join_requested)
		_wrapper.disconnect_signal("p2p_session_connect_fail", _on_p2p_connect_fail)
		_wrapper.disconnect_signal("network_connection_status_changed", _on_network_connection_status_changed)

	if _lobby_id != 0 and _wrapper:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0

	NetwServices.unregister(self)
	NetwServices.unregister(self, LobbyDirectory)


func _process(_dt: float) -> void:
	if _init_ok and _wrapper:
		_wrapper.run_callbacks()


## Returns [code]true[/code] if Steam initialized successfully.
func is_ready() -> bool:
	return _init_ok


## Returns the active lobby ID, or [code]0[/code] when no lobby is joined.
func get_lobby_id() -> int:
	return _lobby_id


## Returns the local user's display name.
func get_persona_name() -> String:
	return _wrapper.get_persona_name() if _init_ok else ""


func get_member_name(peer_id: int) -> String:
	if not _init_ok or _peer == null:
		return super.get_member_name(peer_id)
	var steam_id := _wrapper.get_steam_id_from_peer_id(_peer, peer_id)
	if steam_id == 0:
		return super.get_member_name(peer_id)
	var persona := _wrapper.get_friend_persona_name(steam_id)
	return persona if not persona.is_empty() else super.get_member_name(peer_id)


## Returns the local Steam persona name.
func get_local_member_name() -> String:
	var persona := get_persona_name()
	return persona if not persona.is_empty() else super.get_local_member_name()


func list_lobbies() -> void:
	if not _guard_ready("list_lobbies"):
		lobby_list_updated.emit([] as Array[LobbyInfo])
		return
	_pending_list = true
	Netw.dbg.debug(
		"SteamLobbyDirectory: browsing lobbies with local app_id='%s'.",
		[_local_app_id()],
	)
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


func leave_lobby() -> void:
	_joining = false
	_pending_join_lobby_id = 0
	if _lobby_id == 0 or not _wrapper:
		return
	_wrapper.leave_lobby(_lobby_id)
	_lobby_id = 0
	_peer = null


func make_join_target(lobby: LobbyInfo) -> JoinTarget:
	var target := JoinTarget.new()
	target.display_name = lobby.lobby_name
	target.address = str(lobby.id)
	target.metadata = lobby.metadata.duplicate()
	var steam_backend := SteamBackend.new()
	target.backend = steam_backend
	return target


func host_lobby(server_name: String) -> MultiplayerPeer:
	if not _guard_ready("host_lobby"):
		return null
	if _lobby_id != 0:
		Netw.dbg.warn(
			"SteamLobbyDirectory: host_lobby called while in lobby %d",
			[_lobby_id],
		)
		return null
	_pending_create_name = server_name
	_wrapper.create_lobby(int(default_lobby_type), max_clients)

	var timer := get_tree().create_timer(10.0)
	var timed_out := await Async.timeout(_lobby_created_internal, timer)
	if timed_out:
		Netw.dbg.error("SteamLobbyDirectory: host_lobby timed out.")
		return null
	return _peer


func join_lobby_peer(lobby_id: int) -> MultiplayerPeer:
	if not _guard_ready("join_lobby_peer"):
		return null
	if lobby_id <= 0:
		Netw.dbg.warn(
			"SteamLobbyDirectory: join_lobby_peer invalid ID %d",
			[lobby_id],
		)
		return null
	if reject_own_lobbies and _is_own_lobby(lobby_id):
		Netw.dbg.warn(
			"SteamLobbyDirectory: refusing to join own lobby %d.",
			[lobby_id],
		)
		return null
	if _pending_join_lobby_id != 0:
		Netw.dbg.debug(
			"SteamLobbyDirectory: ignoring join_lobby_peer(%d); " +
			"join_lobby_peer(%d) is still pending.",
			[lobby_id, _pending_join_lobby_id],
		)
		return null
	if _lobby_id != 0:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0
	_joining = true
	_pending_join_lobby_id = lobby_id
	_wrapper.join_lobby(lobby_id)

	var timer := get_tree().create_timer(10.0)
	var timed_out := await Async.timeout(_lobby_joined_internal, timer)
	if timed_out:
		_joining = false
		Netw.dbg.error("SteamLobbyDirectory: join_lobby_peer timed out.")
		return null
	if _peer == null:
		_joining = false
	return _peer


func _bind_tree_signals(mt: MultiplayerTree) -> void:
	if not mt.peer_connected.is_connected(_on_tree_peer_changed):
		mt.peer_connected.connect(_on_tree_peer_changed)
	if not mt.peer_disconnected.is_connected(_on_tree_peer_changed):
		mt.peer_disconnected.connect(_on_tree_peer_changed)
	if not mt.server_disconnecting.is_connected(_on_tree_server_disconnecting):
		mt.server_disconnecting.connect(_on_tree_server_disconnecting)


func _on_tree_peer_changed(_peer_id: int) -> void:
	if _peer_id == 1:
		_joining = false
	if _lobby_id == 0 or not _wrapper:
		return
	var count: int = _wrapper.get_num_lobby_members(_lobby_id)
	_wrapper.set_lobby_data(_lobby_id, "players", str(count))


func _on_tree_server_disconnecting(_reason: String) -> void:
	leave_lobby()


func _guard_ready(op: String) -> bool:
	if not _init_ok:
		Netw.dbg.warn(
			"SteamLobbyDirectory: %s called while Steam is unavailable.",
			[op],
		)
		return false
	return true


# Checks the GodotSteam app id setting before Steam initialization.
func _has_steam_app_id() -> bool:
	if not ProjectSettings.has_setting(STEAM_APP_ID_SETTING):
		return false
	var raw: Variant = ProjectSettings.get_setting(STEAM_APP_ID_SETTING)
	var app_id := str(raw).strip_edges()
	return not app_id.is_empty() and app_id != "0"


# Applies the opt-in Spacewar app id fallback without saving it.
func _apply_spacewar_fallback() -> void:
	ProjectSettings.set_setting(STEAM_APP_ID_SETTING, SPACEWAR_APP_ID)
	var reason := _steam_app_id_fallback_message()
	Netw.dbg.warn(
		"SteamLobbyDirectory: %s",
		[reason],
		func(m): push_warning(m)
	)


# Returns the actionable Steam app id setup hint.
func _steam_app_id_required_message() -> String:
	return (
			"Project setting `%s` must not be empty or 0. Set it to " +
			"your Steam app id, or use 480 for Spacewar while testing " +
			"before you have one."
	) % STEAM_APP_ID_SETTING


# Returns the opt-in Spacewar fallback warning.
func _steam_app_id_fallback_message() -> String:
	return (
			"Project setting `%s` is empty or 0. Using 480 (Spacewar) " +
			"because `allow_spacewar_fallback` is enabled. Set this " +
			"project setting to your Steam app id before publishing."
	) % STEAM_APP_ID_SETTING


func _on_lobby_created(connect_result: int, lobby_id: int) -> void:
	if connect_result != 1:
		var reason := "Lobby create failed (code %d)" % connect_result
		Netw.dbg.error("SteamLobbyDirectory: %s", [reason])
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
	_wrapper.set_lobby_data(lobby_id, "max", str(max_clients))
	Netw.dbg.debug(
		"SteamLobbyDirectory: advertising lobby %d with app_id='%s'.",
		[lobby_id, _local_app_id()],
	)

	var peer := _build_peer()
	if peer == null:
		_lobby_created_internal.emit(null)
		return
	var err: Error = peer.call(&"host_with_lobby", lobby_id)
	if err != OK:
		Netw.dbg.error(
			"SteamLobbyDirectory: host_with_lobby failed: %s",
			[error_string(err)],
		)
		_lobby_created_internal.emit(null)
		return

	_peer = peer
	Netw.dbg.info("SteamLobbyDirectory: lobby %d hosted.", [lobby_id])
	_lobby_created_internal.emit(peer)


func _on_lobby_joined(
		lobby_id: int,
		_permissions: int,
		_locked: bool,
		response: int,
) -> void:
	if response != 1:
		var reason := SteamWrapper.chat_room_enter_response_to_string(response)
		Netw.dbg.error("SteamLobbyDirectory: join failed: %s", [reason])
		_pending_join_lobby_id = 0
		_lobby_joined_internal.emit(null)
		return

	# Host's own joinLobby callback fires too - skip if already hosting.
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
		Netw.dbg.error(
			"SteamLobbyDirectory: connect_to_lobby failed: %s",
			[error_string(err)],
		)
		_pending_join_lobby_id = 0
		_lobby_id = 0
		_wrapper.leave_lobby(lobby_id)
		_lobby_joined_internal.emit(null)
		return

	_peer = peer
	_pending_join_lobby_id = 0
	Netw.dbg.info("SteamLobbyDirectory: joined lobby %d.", [lobby_id])
	_lobby_joined_internal.emit(peer)


func _on_lobby_match_list(lobbies: Array) -> void:
	if not _pending_list:
		return
	_pending_list = false

	var out: Array[LobbyInfo] = []
	for raw_id in lobbies:
		var id := int(raw_id)
		if reject_own_lobbies and _is_own_lobby(id):
			Netw.dbg.warn(
				"SteamLobbyDirectory: ignoring own lobby %d in browse " +
				"results. Steam local testing requires a second account.",
				[id],
				func(m): push_warning(m)
			)
			continue

		var uid := _wrapper.get_lobby_data(id, "uid")
		if (
				not browser_filter_uid.is_empty()
				and uid != browser_filter_uid
		):
			continue
		var info := LobbyInfo.make(
			id,
			_wrapper.get_lobby_data(id, "name"),
			_wrapper.get_num_lobby_members(id),
			_wrapper.get_lobby_member_limit(id),
			{
				"host": _wrapper.get_lobby_data(id, "host"),
				"uid": _wrapper.get_lobby_data(id, "uid"),
				"app_id": _wrapper.get_lobby_data(id, "app_id"),
			},
		)
		Netw.dbg.debug(
			"SteamLobbyDirectory: discovered lobby %d with app_id='%s'.",
			[id, String(info.metadata.get("app_id", ""))],
		)
		out.append(info)
	lobby_list_updated.emit(out)


func _on_join_requested(lobby_id: int, friend_id: int) -> void:
	invite_received.emit(lobby_id, friend_id)


func _build_peer() -> MultiplayerPeer:
	var peer := _wrapper.create_peer()
	if peer == null:
		Netw.dbg.error(
			"SteamLobbyDirectory: SteamMultiplayerPeer class not available.",
		)
		return null
	_wrapper.configure_peer(peer, not disable_nagle, allow_p2p_relay)
	return peer


# The hosting tree's build tag, advertised so browsers can flag a lobby they
# would be rejected from before they try to join.
func _local_app_id() -> String:
	var mt := MultiplayerTree.resolve(self)
	return String(mt.app_id) if mt else ""


func _is_own_lobby(lobby_id: int) -> bool:
	if _wrapper == null:
		return false
	var local_id := _wrapper.get_steam_id()
	return local_id != 0 and _wrapper.get_lobby_owner(lobby_id) == local_id


func _on_p2p_connect_fail(steam_id: int, error: int) -> void:
	if _joining:
		Netw.dbg.warn("SteamLobbyDirectory: P2P connect failed to %d (error %d)" % [steam_id, error])
		_joining = false
		peer_connect_failed.emit("P2P session connect fail")


func _on_network_connection_status_changed(info: Dictionary) -> void:
	if not _joining:
		return
	var state: int = info.get("connection_state", -1)
	if state == 4 or state == 5: # ClosedByPeer or ProblemDetectedLocally
		Netw.dbg.warn("SteamLobbyDirectory: connection status failed (%d)" % state)
		_joining = false
		peer_connect_failed.emit("Network connection status failed")
