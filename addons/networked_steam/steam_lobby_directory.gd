## A [LobbyDirectory] for Steam lobbies.
##
## A member is a Steam user in the joined lobby, identified by their Steam ID.
## [br][br]
## [NetwConnection] hosts and joins lobbies. [SteamSocial] invites friends
## and turns a Steam invite into [signal NetwConnection.invited].
## [codeblock]
## @onready var lobby: SteamLobbyDirectory = $SteamLobbyDirectory
## @onready var steam: SteamSocial = session.service(SteamSocial)
##
## func _ready() -> void:
##     lobby.member_joined.connect(_on_member_joined)
##     lobby.member_left.connect(_on_member_left)
##
## func _on_member_joined(steam_id: int) -> void:
##     var row := Button.new()
##     row.name = str(steam_id)
##     row.text = steam.profile(steam_id).display_name
##     $Members.add_child(row)
##
## func _on_member_left(
##     steam_id: int, _reason: SteamLobbyDirectory.LeaveReason,
## ) -> void:
##     $Members.get_node(str(steam_id)).queue_free()
## [/codeblock]
## An export GodotSteam does not support omits this node and every script that
## names SteamLobbyDirectory. A script that must stay in those exports uses
## [LobbyDirectory] as its type.
class_name SteamLobbyDirectory
extends LobbyDirectory

## Emitted when a Steam user enters the joined lobby.
signal member_joined(steam_id: int)

## Emitted when a Steam user leaves the joined lobby.
signal member_left(steam_id: int, reason: LeaveReason)

## Emitted when the joined lobby's data changes.
signal data_changed

## Emitted when a Steam user's lobby data changes.
signal member_data_changed(steam_id: int)

## Why a Steam user left a lobby.
enum LeaveReason {
	## The user left.
	LEFT,
	## The user's connection dropped.
	DISCONNECTED,
	## The lobby owner removed the user.
	KICKED,
	## The lobby owner banned the user.
	BANNED,
}

## The bundled GodotSteam version.
const GODOTSTEAM_VERSION: String = "4.18.1"

const _SteamStart := preload("steam_start.gd")

## The player limit when hosting supplies none.
@export_range(1, 250) var max_players: int = 8

## Only lobbies with this tag appear in the browser.
@export var browser_tag: String = "networked"

## Lets Steam relay traffic when a direct connection fails.
@export var allow_relay: bool = true

## Sends packets without waiting to combine them.
@export var no_delay: bool = true

## Uses Steam's Spacewar test app when no app id is set.
@export var spacewar_when_unset: bool = false

## Hides lobbies owned by this Steam account.
@export var hide_own_lobbies: bool = true

## The joined lobby id, or zero while outside a lobby. Its address is this
## id as a [String].
var lobby_id: int = 0

## The lobby's peer, or [code]null[/code] while disconnected.
var peer: SteamMultiplayerPeer

## The Steam IDs in the joined lobby.
var members: Array[int]:
	get:
		var result: Array[int] = []
		for index: int in Steam.getNumLobbyMembers(lobby_id):
			result.append(Steam.getLobbyMemberByIndex(lobby_id, index))
		return result

static var _creator: WeakRef
var _operation: int = 0
var _awaiting_lobby: bool = false
var _session: NetwSession


func _init() -> void:
	display_name = "Steam"
	address_label = "Lobby ID"
	address_help = "Paste the Steam lobby id shared by the host."
	list_interval = 5.0
	capabilities = (
			LobbyDirectory.CAPABILITY_BROWSE
			| LobbyDirectory.CAPABILITY_FRIENDS_ONLY_SUPPORT
			| LobbyDirectory.CAPABILITY_INVITES
			| LobbyDirectory.CAPABILITY_FRIEND_NAMES
	)


func _should_register() -> bool:
	return Steam.isSteamRunning()


func _service_entered(session: NetwSession) -> void:
	var app_id := int(
		ProjectSettings.get_setting("steam/initialization/app_id", 0),
	)
	if app_id == 0 and spacewar_when_unset:
		app_id = 480
	if app_id == 0:
		provider_unavailable.emit(
			"Set steam/initialization/app_id to your Steam app id.",
		)
		session.unregister_service(self)
		return

	var started := _SteamStart.start(app_id)
	if started.status != Steam.STEAM_API_INIT_RESULT_OK:
		provider_unavailable.emit(
			"Steam did not start. " + String(started.verbal),
		)
		session.unregister_service(self)
		return

	_session = session
	if not Steam.lobby_created.is_connected(_on_lobby_created):
		Steam.lobby_created.connect(_on_lobby_created)

	var version: String = Steam.get_godotsteam_version()
	if version != GODOTSTEAM_VERSION:
		push_warning(
			"GodotSteam %s is installed. Install %s for this Networked release." % [
				version,
				GODOTSTEAM_VERSION,
			],
		)

	Steam.lobby_match_list.connect(_on_lobby_match_list)
	Steam.lobby_chat_update.connect(_on_lobby_chat_update)
	Steam.lobby_data_update.connect(_on_lobby_data_update)


func _service_exiting(_departing: NetwSession) -> void:
	if _session == null:
		return

	_session = null
	Steam.lobby_match_list.disconnect(_on_lobby_match_list)
	Steam.lobby_chat_update.disconnect(_on_lobby_chat_update)
	Steam.lobby_data_update.disconnect(_on_lobby_data_update)

	_leave_lobby()


func _peer_class() -> StringName:
	return &"SteamMultiplayerPeer"


func _join_address() -> String:
	return str(lobby_id) if lobby_id != 0 else ""


func _member_name(peer_id: int) -> String:
	if peer == null:
		return LobbyDirectory.member_name_default(peer_id)

	var steam_id := peer.get_steam_id_for_peer_id(peer_id)
	return Steam.getFriendPersonaName(steam_id)


func _account_of(peer_id: int) -> String:
	if peer == null:
		return ""
	var steam_id: int = peer.get_steam_id_for_peer_id(peer_id)
	return "steam:" + str(steam_id) if steam_id != 0 else ""


func _local_member_name() -> String:
	return Steam.getPersonaName()


func _host_lobby(settings: Dictionary) -> void:
	if _creator != null or _awaiting_lobby:
		fail(ERR_BUSY, "Steam is opening a lobby. Wait for it to finish.")
		return

	var visibility: int = settings.get(
		"visibility",
		NetwServerInfo.VISIBILITY_PUBLIC,
	)
	var types := {
		NetwServerInfo.VISIBILITY_PRIVATE: Steam.LOBBY_TYPE_PRIVATE,
		NetwServerInfo.VISIBILITY_FRIENDS_ONLY: Steam.LOBBY_TYPE_FRIENDS_ONLY,
		NetwServerInfo.VISIBILITY_PUBLIC: Steam.LOBBY_TYPE_PUBLIC,
	}
	if not types.has(visibility):
		fail(ERR_INVALID_PARAMETER, "Choose a valid lobby visibility.")
		return

	_creator = weakref(self)
	_awaiting_lobby = true
	_operation += 1
	var operation := _operation
	var limit := int(settings.get("max_players", 0))
	var created: Array = await _SteamStart.create_lobby(
		types[visibility],
		limit if limit > 0 else max_players,
	)
	_awaiting_lobby = false
	if operation != _operation:
		if created[0] == Steam.RESULT_OK:
			Steam.leaveLobby(created[1])
		return

	if created[0] != Steam.RESULT_OK:
		fail(ERR_CANT_CREATE, "Steam refused the lobby.")
		return

	lobby_id = created[1]
	var metadata := {
		"name": str(settings.get("name", "")),
		"uid": browser_tag,
		"app_id": String(_session.config.app_id),
		"host": Steam.getPersonaName(),
		"visibility": str(visibility),
	}
	for key: String in metadata:
		Steam.setLobbyData(lobby_id, key, metadata[key])

	Steam.allowP2PPacketRelay(allow_relay)
	peer = SteamMultiplayerPeer.new()
	peer.no_delay = no_delay
	peer.no_nagle = no_delay
	peer.server_relay = allow_relay
	var error := peer.host_with_lobby(lobby_id)
	if error != OK:
		_leave_lobby()
		fail(error, "Steam could not host the lobby.")
		return
	deliver(peer)


func _join_lobby(address: String) -> void:
	if _awaiting_lobby:
		fail(ERR_BUSY, "Steam is opening a lobby. Wait for it to finish.")
		return

	var target := int(address)
	if target <= 0:
		fail(ERR_INVALID_PARAMETER, "Enter a valid Steam lobby id.")
		return
	if hide_own_lobbies and Steam.getLobbyOwner(target) == Steam.getSteamID():
		fail(
			ERR_UNAUTHORIZED,
			"This account owns the lobby. Use a second Steam account.",
		)
		return

	_operation += 1
	var operation := _operation
	_awaiting_lobby = true
	Steam.joinLobby(target)

	var joined: Array = await Steam.lobby_joined
	while joined[0] != target:
		joined = await Steam.lobby_joined

	_awaiting_lobby = false
	if operation != _operation:
		if joined[3] == Steam.CHAT_ROOM_ENTER_RESPONSE_SUCCESS:
			Steam.leaveLobby(target)
		return

	if joined[3] != Steam.CHAT_ROOM_ENTER_RESPONSE_SUCCESS:
		fail(ERR_CANT_CONNECT, "Steam refused to join the lobby.")
		return

	lobby_id = target
	peer = SteamMultiplayerPeer.new()
	peer.no_delay = no_delay
	peer.no_nagle = no_delay
	peer.server_relay = allow_relay
	var error := peer.connect_to_lobby(lobby_id)
	if error != OK:
		_leave_lobby()
		fail(error, "Steam could not connect to the lobby.")
		return
	deliver(peer)


func _leave_lobby() -> void:
	_operation += 1
	if lobby_id != 0:
		Steam.leaveLobby(lobby_id)
	lobby_id = 0
	peer = null


func _list_lobbies() -> void:
	if not browser_tag.is_empty():
		Steam.addRequestLobbyListStringFilter(
			"uid",
			browser_tag,
			Steam.LOBBY_COMPARISON_EQUAL,
		)

	Steam.addRequestLobbyListDistanceFilter(Steam.LOBBY_DISTANCE_FILTER_WORLDWIDE)
	Steam.requestLobbyList()


func _on_lobby_match_list(lobbies: Array) -> void:
	if not _session.connection.browsing:
		return

	var addresses := PackedStringArray()
	var names := PackedStringArray()
	var infos: Array[NetwServerInfo] = []

	for id: int in lobbies:
		if (
				not browser_tag.is_empty()
				and Steam.getLobbyData(id, "uid") != browser_tag
		):
			continue
		if hide_own_lobbies and Steam.getLobbyOwner(id) == Steam.getSteamID():
			continue

		var info := NetwServerInfo.new()
		info.app_id = StringName(Steam.getLobbyData(id, "app_id"))
		info.players = Steam.getNumLobbyMembers(id)
		info.max_players = Steam.getLobbyMemberLimit(id)
		var visibility: String = Steam.getLobbyData(id, "visibility")
		info.visibility = int(visibility) as NetwServerInfo.Visibility
		info.metadata = { "host": Steam.getLobbyData(id, "host"), "uid": browser_tag }

		addresses.append(str(id))
		names.append(Steam.getLobbyData(id, "name"))
		infos.append(info)

	publish_lobbies(addresses, names, infos)


static func _on_lobby_created(result: int, id: int) -> void:
	if (
			_creator != null
			and _creator.get_ref() == null
			and result == Steam.RESULT_OK
	):
		Steam.leaveLobby(id)
	_creator = null


## Returns the session peer id for [param steam_id], or zero without a peer.
func peer_of(steam_id: int) -> int:
	return peer.get_peer_id_for_steam_id(steam_id) if peer != null else 0


## Returns the Steam id for [param peer_id], or zero without a peer.
func member_of(peer_id: int) -> int:
	return peer.get_steam_id_for_peer_id(peer_id) if peer != null else 0


## Returns the joined lobby's value at [param key].
func data(key: String) -> String:
	return Steam.getLobbyData(lobby_id, key)


## Sets the joined lobby's value at [param key].[br][br][b]Server Only.[/b]
func set_data(key: String, value: String) -> bool:
	if not multiplayer.is_server():
		return false

	return Steam.setLobbyData(lobby_id, key, value)


## Sets the local Steam user's lobby value at [param key].
func set_member_data(key: String, value: String) -> void:
	Steam.setLobbyMemberData(lobby_id, key, value)


## Returns [param steam_id]'s lobby value at [param key].
func member_data(steam_id: int, key: String) -> String:
	return Steam.getLobbyMemberData(lobby_id, steam_id, key)


func _on_lobby_chat_update(id: int, changed: int, _by: int, state: int) -> void:
	if id != lobby_id:
		return

	if state & Steam.CHAT_MEMBER_STATE_CHANGE_ENTERED:
		member_joined.emit(changed)

	var reasons := {
		Steam.CHAT_MEMBER_STATE_CHANGE_BANNED: LeaveReason.BANNED,
		Steam.CHAT_MEMBER_STATE_CHANGE_KICKED: LeaveReason.KICKED,
		Steam.CHAT_MEMBER_STATE_CHANGE_DISCONNECTED: LeaveReason.DISCONNECTED,
		Steam.CHAT_MEMBER_STATE_CHANGE_LEFT: LeaveReason.LEFT,
	}
	for flag: int in reasons:
		if state & flag:
			member_left.emit(changed, reasons[flag])
			return


func _on_lobby_data_update(success: int, id: int, changed: int) -> void:
	if success == 0 or id != lobby_id:
		return

	if changed == lobby_id:
		data_changed.emit()
		return

	member_data_changed.emit(changed)
