## Steam-backed [LobbyProvider] implementation.
##
## Add as a child of [MultiplayerTree]. Owns the [SteamWrapper], drives Steam
## callbacks each frame, and translates Steam signals into provider signals.
## [br][br]
## Only one instance may exist per process; duplicates queue themselves for
## deletion. [member browser_filter_uid] tags hosted lobbies so the browser
## only returns lobbies created with the same game id.
## [codeblock]
## @onready var provider := $MultiplayerTree/SteamLobbyProvider
## func _on_create_pressed() -> void:
##     provider.lobby_created.connect(_on_lobby_created)
##     provider.create_lobby("My Room")
##
## func _on_lobby_created(_id: int) -> void:
##     provider.bind(Netw.tree)
## [/codeblock]
class_name SteamLobbyProvider
extends LobbyProvider


static var _instance: WeakRef = weakref(null)


## Maximum number of simultaneous lobby members.
@export_range(1, 250, 1, "or_greater", "suffix:players") \
var max_clients: int = 8

## Default visibility used by [method create_lobby].
@export var default_lobby_type: SteamWrapper.LobbyType = SteamWrapper.LobbyType.PUBLIC

## Tag stored under the [code]uid[/code] lobby key. Browser filters on this so
## different games don't pollute each other's lobby lists.
@export var browser_filter_uid: String = "networked"

## If [code]true[/code], OS-level Steam invites automatically disconnect any
## current session and join the invited lobby.
@export var auto_join_on_invite: bool = true

## If [code]true[/code], disables Nagle's algorithm on the produced peer.
@export var disable_nagle: bool = true

## If [code]true[/code], allows Steam to relay traffic when direct P2P fails.
@export var allow_p2p_relay: bool = true


var _wrapper: SteamWrapper
var _lobby_id: int = 0
var _peer: MultiplayerPeer
var _pending_list: bool = false
var _pending_create_name: String = ""
var _init_ok: bool = false


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	var existing: SteamLobbyProvider = _instance.get_ref()
	if existing and existing != self:
		push_error(
			"SteamLobbyProvider: only one instance is allowed. " +
			"Queueing duplicate for deletion."
		)
		queue_free()
		return
	_instance = weakref(self)

	_wrapper = SteamWrapper.new()
	if not _wrapper.is_available():
		_init_ok = false
		Netw.dbg.warn(
			"SteamLobbyProvider: GodotSteam singleton not found."
		)
		provider_unavailable.emit.call_deferred(
			"GodotSteam singleton not found"
		)
		return

	var init_res: Dictionary = _wrapper.steam_init_ex()
	var status: int = init_res.get("status", SteamWrapper.InitResult.FAILED_GENERIC)
	_init_ok = status == SteamWrapper.InitResult.OK
	if not _init_ok:
		var reason := "Steam init failed (status %d)" % status
		if status == SteamWrapper.InitResult.NO_STEAM_CLIENT:
			reason += ": no Steam client running"
		Netw.dbg.error("SteamLobbyProvider: %s", [reason])
		provider_unavailable.emit.call_deferred(reason)
		return

	_wrapper.connect_signal("lobby_created", _on_lobby_created)
	_wrapper.connect_signal("lobby_joined", _on_lobby_joined)
	_wrapper.connect_signal("lobby_match_list", _on_lobby_match_list)
	_wrapper.connect_signal("join_requested", _on_join_requested)

	NetwServices.register(self, LobbyProvider)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	var existing: SteamLobbyProvider = _instance.get_ref()
	if existing == self:
		_instance = weakref(null)

	if _wrapper and _wrapper.is_available():
		_wrapper.disconnect_signal("lobby_created", _on_lobby_created)
		_wrapper.disconnect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.disconnect_signal("lobby_match_list", _on_lobby_match_list)
		_wrapper.disconnect_signal("join_requested", _on_join_requested)

	if _lobby_id != 0 and _wrapper:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0

	NetwServices.unregister(self, LobbyProvider)


func _process(_dt: float) -> void:
	if _init_ok and _wrapper:
		_wrapper.run_callbacks()


## Returns [code]true[/code] if Steam initialized successfully.
func is_ready() -> bool:
	return _init_ok


## Returns the locally-owned [MultiplayerPeer], or [code]null[/code].
func get_peer() -> MultiplayerPeer:
	return _peer


## Returns the active lobby ID, or [code]0[/code] when no lobby is joined.
func get_lobby_id() -> int:
	return _lobby_id


## Returns the local user's display name.
func get_persona_name() -> String:
	return _wrapper.get_persona_name() if _init_ok else ""


func create_lobby(lobby_name: String) -> void:
	if not _guard_ready("create_lobby"):
		return
	if _lobby_id != 0:
		Netw.dbg.warn(
			"SteamLobbyProvider: create_lobby called while in lobby %d",
			[_lobby_id]
		)
		lobby_join_failed.emit("Already in a lobby")
		return
	_pending_create_name = lobby_name
	_wrapper.create_lobby(int(default_lobby_type), max_clients)


func join_lobby(lobby_id: int) -> void:
	if not _guard_ready("join_lobby"):
		return
	if lobby_id <= 0:
		lobby_join_failed.emit("Invalid lobby id")
		return
	if _lobby_id != 0:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0
	_wrapper.join_lobby(lobby_id)


func list_lobbies() -> void:
	if not _guard_ready("list_lobbies"):
		lobby_list_updated.emit([] as Array[LobbyInfo])
		return
	_pending_list = true
	if not browser_filter_uid.is_empty():
		_wrapper.add_request_lobby_list_string_filter(
			"uid", browser_filter_uid, SteamWrapper.LobbyComparison.EQUAL
		)
	_wrapper.add_request_lobby_list_distance_filter(
		SteamWrapper.LobbyDistance.WORLDWIDE
	)
	_wrapper.request_lobby_list()


func leave_lobby() -> void:
	if _lobby_id == 0 or not _wrapper:
		return
	_wrapper.leave_lobby(_lobby_id)
	_lobby_id = 0
	_peer = null


func _bind_tree_signals(tree: NetwTree) -> void:
	super._bind_tree_signals(tree)
	if not tree.peer_connected.is_connected(_on_tree_peer_changed):
		tree.peer_connected.connect(_on_tree_peer_changed)
	if not tree.peer_disconnected.is_connected(_on_tree_peer_changed):
		tree.peer_disconnected.connect(_on_tree_peer_changed)


func _on_tree_peer_changed(_peer_id: int) -> void:
	if _lobby_id == 0 or not _wrapper:
		return
	var count: int = _wrapper.get_num_lobby_members(_lobby_id)
	_wrapper.set_lobby_data(_lobby_id, "players", str(count))


func _guard_ready(op: String) -> bool:
	if not _init_ok:
		Netw.dbg.warn(
			"SteamLobbyProvider: %s called while Steam is unavailable.",
			[op]
		)
		lobby_join_failed.emit("Steam unavailable")
		return false
	return true


func _on_lobby_created(connect_result: int, lobby_id: int) -> void:
	if connect_result != 1:
		var reason := "Lobby create failed (code %d)" % connect_result
		Netw.dbg.error("SteamLobbyProvider: %s", [reason])
		_pending_create_name = ""
		lobby_join_failed.emit(reason)
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
	_wrapper.set_lobby_data(lobby_id, "host", _wrapper.get_persona_name())
	_wrapper.set_lobby_data(lobby_id, "players", "1")
	_wrapper.set_lobby_data(lobby_id, "max", str(max_clients))

	var peer := _build_peer()
	if peer == null:
		lobby_join_failed.emit("Failed to instantiate SteamMultiplayerPeer")
		return
	var err: Error = peer.call(&"host_with_lobby", lobby_id)
	if err != OK:
		Netw.dbg.error(
			"SteamLobbyProvider: host_with_lobby failed: %s",
			[error_string(err)]
		)
		lobby_join_failed.emit(
			"host_with_lobby failed: %s" % error_string(err)
		)
		return

	_peer = peer
	Netw.dbg.info("SteamLobbyProvider: lobby %d hosted.", [lobby_id])
	peer_ready.emit(peer)
	lobby_created.emit(lobby_id)


func _on_lobby_joined(
	lobby_id: int, _permissions: int, _locked: bool, response: int
) -> void:
	if response != 1:
		var reason := SteamWrapper.chat_room_enter_response_to_string(response)
		Netw.dbg.error("SteamLobbyProvider: join failed: %s", [reason])
		lobby_join_failed.emit(reason)
		return

	# Host's own joinLobby callback fires too - skip if already hosting.
	if _peer != null and _lobby_id == lobby_id:
		return

	_lobby_id = lobby_id
	var peer := _build_peer()
	if peer == null:
		lobby_join_failed.emit("Failed to instantiate SteamMultiplayerPeer")
		return
	var err: Error = peer.call(&"connect_to_lobby", lobby_id)
	if err != OK:
		Netw.dbg.error(
			"SteamLobbyProvider: connect_to_lobby failed: %s",
			[error_string(err)]
		)
		lobby_join_failed.emit(
			"connect_to_lobby failed: %s" % error_string(err)
		)
		return

	_peer = peer
	Netw.dbg.info("SteamLobbyProvider: joined lobby %d.", [lobby_id])
	peer_ready.emit(peer)
	lobby_joined.emit(lobby_id)


func _on_lobby_match_list(lobbies: Array) -> void:
	if not _pending_list:
		return
	_pending_list = false

	var out: Array[LobbyInfo] = []
	for raw_id in lobbies:
		var id := int(raw_id)
		var info := LobbyInfo.make(
			id,
			_wrapper.get_lobby_data(id, "name"),
			_wrapper.get_num_lobby_members(id),
			_wrapper.get_lobby_member_limit(id),
			{
				"host": _wrapper.get_lobby_data(id, "host"),
				"uid": _wrapper.get_lobby_data(id, "uid"),
			}
		)
		out.append(info)
	lobby_list_updated.emit(out)


func _on_join_requested(lobby_id: int, friend_id: int) -> void:
	invite_received.emit(lobby_id, friend_id)
	if not auto_join_on_invite:
		return

	var mt: MultiplayerTree = MultiplayerTree.resolve(self)
	if not mt:
		Netw.dbg.warn(
			"SteamLobbyProvider: invite for %d but no tree resolved.",
			[lobby_id]
		)
		return

	_auto_join_flow(mt, lobby_id)


func _auto_join_flow(mt: MultiplayerTree, lobby_id: int) -> void:
	if mt.state == MultiplayerTree.State.ONLINE:
		await mt.disconnect_player()
	join_lobby(lobby_id)


func _build_peer() -> MultiplayerPeer:
	var peer := _wrapper.create_peer()
	if peer == null:
		Netw.dbg.error(
			"SteamLobbyProvider: SteamMultiplayerPeer class not available."
		)
		return null
	_wrapper.configure_peer(peer, not disable_nagle, allow_p2p_relay)
	return peer
