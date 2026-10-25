## Connects the player with their Steam friends and lets them join the
## player's session from Steam.
##
## [codeblock]
## @onready var steam: SteamSocial = session.service(SteamSocial)
##
## func _ready() -> void:
##     for friend in steam.friends:
##         friends_list.add_item(steam.profile(friend).display_name)
##     session.connection.invited.connect(_on_invited)
##
## func _on_friends_list_item_activated(index: int) -> void:
##     steam.invite_friend(steam.friends[index])
##
## func _on_invited(endpoint: NetwEndpoint) -> void:
##     await session.connection.join(endpoint).settled
## [/codeblock]
## With two sessions in one game, such as split screen, only the first
## [SteamSocial] to enter the tree handles Steam invites and runs
## [member party], because Steam signs in one player per game.
class_name SteamSocial
extends NetwService

## Emitted when [member friends] gains or loses a friend, and when a friend's
## name or game changes.
signal friends_changed

const _SteamStart := preload("steam_start.gd")
const _STEAM_PEER := &"SteamMultiplayerPeer"
const _CONNECT := "+connect "
const _CONNECT_LOBBY := "+connect_lobby "

## Uses Steam's Spacewar test app when no app id is set.
@export var spacewar_when_unset: bool = false

## The Steam IDs of the player's friends.
var friends: Array[int]:
	get:
		var ids: Array[int] = []
		for index: int in Steam.getFriendCount(Steam.FRIEND_FLAG_IMMEDIATE):
			ids.append(
				Steam.getFriendByIndex(index, Steam.FRIEND_FLAG_IMMEDIATE),
			)
		return ids

## The player's [SteamParty] of Steam friends. It is read-only.
var party := SteamParty.new(self)

static var _owner: SteamSocial
static var _launch_read: bool
var _session: NetwSession
var _profiles: Dictionary[int, SteamProfile]
var _presence: String


func _should_register() -> bool:
	return Steam.isSteamRunning()


func _service_entered(session: NetwSession) -> void:
	var app_id := int(
		ProjectSettings.get_setting("steam/initialization/app_id", 0),
	)
	if app_id == 0 and spacewar_when_unset:
		app_id = 480
	var started := _SteamStart.start(app_id)
	if started.status != Steam.STEAM_API_INIT_RESULT_OK:
		push_warning(
			"SteamSocial is off because Steam did not start (%s). " % [
				started.verbal,
			]
			+ "Run the game signed in to Steam and set "
			+ "steam/initialization/app_id.",
		)
		session.unregister_service(self)
		return

	_session = session
	Steam.persona_state_change.connect(_on_persona_state_change)
	Steam.avatar_loaded.connect(_on_avatar_loaded)
	if _owner != null:
		return

	_owner = self
	Steam.join_requested.connect(_on_join_requested)
	Steam.join_game_requested.connect(_on_join_game_requested)
	session.state_changed.connect(_on_session_changed)
	session.local_joined.connect(_on_session_changed)
	session.connection.join_address_changed.connect(_on_session_changed)
	Steam.lobby_invite.connect(party._on_lobby_invite)
	Steam.lobby_joined.connect(party._on_lobby_joined)
	Steam.lobby_chat_update.connect(party._on_lobby_chat_update)
	Steam.lobby_data_update.connect(party._on_lobby_data_update)
	get_tree().process_frame.connect(party._on_process_frame)
	if _launch_read:
		return

	_launch_read = true
	var launched := " ".join(OS.get_cmdline_args()) + " " \
			+ Steam.getLaunchCommandLine()
	var lobby := launched.get_slice(_CONNECT_LOBBY, 1).get_slice(" ", 0)
	if lobby.is_valid_int():
		_on_join_requested.call_deferred(int(lobby), 0)
	if launched.contains(_CONNECT):
		_on_join_game_requested.call_deferred(
			0,
			_CONNECT + launched.get_slice(_CONNECT, 1).get_slice(" ", 0),
		)


func _service_exiting(_departing: NetwSession) -> void:
	if _session == null:
		return

	Steam.persona_state_change.disconnect(_on_persona_state_change)
	Steam.avatar_loaded.disconnect(_on_avatar_loaded)
	if _owner == self:
		Steam.join_requested.disconnect(_on_join_requested)
		Steam.join_game_requested.disconnect(_on_join_game_requested)
		_session.state_changed.disconnect(_on_session_changed)
		_session.local_joined.disconnect(_on_session_changed)
		_session.connection.join_address_changed.disconnect(
			_on_session_changed,
		)
		Steam.lobby_invite.disconnect(party._on_lobby_invite)
		Steam.lobby_joined.disconnect(party._on_lobby_joined)
		Steam.lobby_chat_update.disconnect(party._on_lobby_chat_update)
		Steam.lobby_data_update.disconnect(party._on_lobby_data_update)
		get_tree().process_frame.disconnect(party._on_process_frame)
		party.leave()
		if not _presence.is_empty():
			Steam.setRichPresence("connect", "")
		_presence = ""
		_owner = null
	_session = null


## Returns the profile of [param steam_id], the same object on every call.
func profile(steam_id: int) -> SteamProfile:
	if not _profiles.has(steam_id):
		_profiles[steam_id] = SteamProfile.new(self, steam_id)
		if not Steam.requestUserInformation(steam_id, false):
			Steam.getPlayerAvatar(Steam.AVATAR_MEDIUM, steam_id)
	return _profiles[steam_id]


## Invites [param steam_id] into the session, and returns [code]false[/code]
## while it has no join address.
func invite_friend(steam_id: int) -> bool:
	var connect := _connect_string()
	if connect.begins_with(_CONNECT_LOBBY):
		return Steam.inviteUserToLobby(
			int(connect.trim_prefix(_CONNECT_LOBBY)),
			steam_id,
		)
	return not connect.is_empty() and Steam.inviteUserToGame(steam_id, connect)


## Opens Steam's invite dialog for the session.
func open_invite_dialog() -> void:
	var connect := _connect_string()
	if connect.begins_with(_CONNECT_LOBBY):
		Steam.activateGameOverlayInviteDialog(
			int(connect.trim_prefix(_CONNECT_LOBBY)),
		)
		return
	Steam.activateGameOverlayInviteDialogConnectString(connect)


func _connect_string() -> String:
	if _session == null or not _session.is_online:
		return ""
	var endpoint := _session.connection.current_endpoint
	var address := _session.connection.join_address
	if endpoint == null or address.is_empty():
		return ""
	if endpoint.peer_class == _STEAM_PEER:
		return _CONNECT_LOBBY + address
	return _CONNECT + "%s|%s" % [endpoint.peer_class, address]


func _on_session_changed(_value: Variant) -> void:
	var connect := _connect_string()
	if _session.local_player == null or connect.begins_with(_CONNECT_LOBBY):
		connect = ""
	if connect == _presence:
		return
	_presence = connect
	Steam.setRichPresence("connect", connect)


func _on_join_requested(lobby_id: int, friend_id: int) -> void:
	if await party._is_party(lobby_id):
		party._move_to(lobby_id)
		return
	if _session != null:
		_session.connection.invite(
			_STEAM_PEER,
			str(lobby_id),
			Steam.getFriendPersonaName(friend_id) if friend_id != 0 else "",
		)


func _on_join_game_requested(friend_id: int, connect: String) -> void:
	match Array(connect.trim_prefix(_CONNECT).split("|", true, 1)):
		[var peer_class, var address] when (
				connect.begins_with(_CONNECT)
				and not peer_class.is_empty() and not address.is_empty()
		):
			_session.connection.invite(
				StringName(peer_class),
				address,
				Steam.getFriendPersonaName(friend_id) if friend_id != 0 else "",
			)


func _on_persona_state_change(steam_id: int, _flags: int) -> void:
	if _profiles.has(steam_id):
		Steam.getPlayerAvatar(Steam.AVATAR_MEDIUM, steam_id)
		_profiles[steam_id].changed.emit()
	if steam_id in friends:
		friends_changed.emit()


func _on_avatar_loaded(steam_id: int, size: int, data: PackedByteArray) -> void:
	if not _profiles.has(steam_id) or size <= 0:
		return
	_profiles[steam_id]._avatar = ImageTexture.create_from_image(
		Image.create_from_data(size, size, false, Image.FORMAT_RGBA8, data),
	)
	_profiles[steam_id].changed.emit()
