## The player's party of Steam friends, which stays together across matches
## and servers.
##
## A player is in a party of one, leading it, until a friend joins. Each
## [SteamSocial] has one as [member SteamSocial.party].
## [br][br]
## A friend joins by accepting an invitation in Steam's overlay or chat, and
## the player joins a friend's party the same way.
## [br][br]
## Only the leader can [method kick].
## [br][br]
## Each member follows the leader's server through
## [signal NetwConnection.invited].
## [codeblock]
## func _ready() -> void:
##     steam.party.invited.connect(_on_party_invited)
##
## func _on_friend_activated(index: int) -> void:
##     steam.party.invite(steam.friends[index])
##
## func _on_party_invited(friend: int) -> void:
##     notice.text = "%s invited you. Accept in Steam." % [
##         steam.profile(friend).display_name,
##     ]
## [/codeblock]
class_name SteamParty
extends RefCounted

## Emitted when [member members], [member leader] or [member invitations]
## change.
signal changed
## Emitted when the friend [param friend] invites the player to their party.
## The player accepts it in Steam.
signal invited(friend: int)

const _SteamStart := preload("steam_start.gd")
const _PARTY_KEY := "netw"
const _PARTY := "party"
const _JOIN_KEY := "join"
const _MOVE_WAIT_MSEC := 30000
const _ANSWER_WAIT_MSEC := 5000
const _OPEN_ATTEMPTS := 3

## The most members the party takes. Steam takes at most 250.
var max_members := 8:
	set(value):
		max_members = value
		if _lobby != 0 and leader == Steam.getSteamID():
			Steam.setLobbyMemberLimit(_lobby, clampi(value, 1, 250))
		changed.emit()

## Sends the leader's server to [signal NetwConnection.invited] each time
## the leader hosts or joins one, with [member NetwEndpoint.invited_by] set
## to the leader's display name. Keep it on for one party at a time, or two
## leaders pull the player between their servers.
var follow_leader := true

## The Steam IDs in the party, the player included, lowest first.
var members: Array[int]:
	get:
		var ids: Array[int] = []
		if SteamSocial._owner != _social:
			return ids
		var own := Steam.getSteamID()
		ids.append(own)
		if _lobby == 0:
			return ids
		for index: int in Steam.getNumLobbyMembers(_lobby):
			var id := Steam.getLobbyMemberByIndex(_lobby, index)
			if id != own:
				ids.append(id)
		ids.sort()
		return ids

## The member who owns the party's Steam lobby. Steam picks another when the
## leader leaves.
var leader: int:
	get:
		if SteamSocial._owner != _social:
			return 0
		var owner := Steam.getLobbyOwner(_lobby) if _lobby != 0 else 0
		return owner if owner != 0 else Steam.getSteamID()

## The friends who invited the player to their party. An invitation leaves
## the list when the player joins that party.
var invitations: Array[int]:
	get:
		var ids: Array[int] = []
		ids.assign(_invitations.keys())
		return ids

var _social: SteamSocial
var _lobby: int
var _joining: int
var _old_lobby: int
var _old_until: int
var _kicked: int
var _opening: bool
var _serial: int
var _inviting: Array[int]
var _invitations: Dictionary[int, int]
var _answered: Dictionary[int, int]
var _followed: String


func _init(social: SteamSocial) -> void:
	_social = social
	changed.connect(_on_changed)


func _on_changed() -> void:
	var leading := leader
	if not follow_leader or _lobby == 0 or leading == Steam.getSteamID():
		return

	var server := Steam.getLobbyData(_lobby, _JOIN_KEY)
	if server == _followed:
		return
	_followed = server
	match Array(server.split("|", true, 1)):
		[var peer_class, var address] when (
				not peer_class.is_empty() and not address.is_empty()
		):
			Netw.session(_social).connection.invite(
				StringName(peer_class),
				address,
				_social.profile(leading).display_name,
			)


func _on_process_frame() -> void:
	var own := Steam.getSteamID()
	if _lobby != 0 and leader == own:
		var session := Netw.session(_social)
		var endpoint := session.connection.current_endpoint
		var address := session.connection.join_address
		var server := ""
		if session.is_online and session.local_player != null \
				and endpoint != null and not endpoint.peer_class.is_empty() \
				and not address.is_empty():
			server = "%s|%s" % [endpoint.peer_class, address]
		if server != Steam.getLobbyData(_lobby, _JOIN_KEY):
			Steam.setLobbyData(_lobby, _JOIN_KEY, server)

	if _old_lobby == 0 or _joining != 0 or _opening:
		return
	var moved := true
	if _kicked != 0:
		var arrived := members
		for index: int in Steam.getNumLobbyMembers(_old_lobby):
			var id := Steam.getLobbyMemberByIndex(_old_lobby, index)
			moved = moved and (id in [own, _kicked] or id in arrived)
	if moved or Time.get_ticks_msec() >= _old_until:
		Steam.leaveLobby(_old_lobby)
		_old_lobby = 0
		_kicked = 0


func _on_lobby_invite(inviter: int, lobby: int, _game: int) -> void:
	if lobby in [_lobby, _old_lobby, _joining] or not await _is_party(lobby):
		return

	if _lobby != 0 and inviter == leader:
		_move_to(lobby)
		return
	_invitations[inviter] = lobby
	changed.emit()
	invited.emit(inviter)


func _on_lobby_joined(
		lobby: int,
		_permissions: int,
		_locked: bool,
		response: int,
) -> void:
	var entered := response == Steam.CHAT_ROOM_ENTER_RESPONSE_SUCCESS
	if lobby != _joining:
		if entered and lobby not in [_lobby, _old_lobby] \
				and Steam.getLobbyData(lobby, _PARTY_KEY) == _PARTY \
				and Steam.getLobbyOwner(lobby) != Steam.getSteamID():
			Steam.leaveLobby(lobby)
		return

	_joining = 0
	if not entered:
		push_warning(
			"Steam did not let the player into the party (%d). " % response
			+ "Ask for a new invitation.",
		)
		_lobby = _old_lobby
		_old_lobby = 0
		changed.emit()
		return
	_lobby = lobby
	_followed = ""
	for friend: int in _inviting:
		Steam.inviteUserToLobby(_lobby, friend)
	_inviting.clear()
	changed.emit()


func _on_lobby_chat_update(
		lobby: int,
		_changed_id: int,
		_making_change_id: int,
		_chat_state: int,
) -> void:
	if lobby == _lobby:
		changed.emit()


func _on_lobby_data_update(_success: int, lobby: int, member: int) -> void:
	if lobby != member:
		return
	_answered[lobby] = Time.get_ticks_msec()
	if lobby == _lobby:
		changed.emit()


func _is_party(lobby: int) -> bool:
	var own := Steam.getSteamID()
	var inside := false
	for index: int in Steam.getNumLobbyMembers(lobby):
		inside = inside or Steam.getLobbyMemberByIndex(lobby, index) == own
	var tagged := Steam.getLobbyData(lobby, _PARTY_KEY) == _PARTY
	if inside or tagged:
		return tagged
	var asked := Time.get_ticks_msec()
	if not Steam.requestLobbyData(lobby):
		return false
	while _answered.get(lobby, -1) < asked \
			and Time.get_ticks_msec() < asked + _ANSWER_WAIT_MSEC:
		await _social.get_tree().process_frame
	return Steam.getLobbyData(lobby, _PARTY_KEY) == _PARTY


func _move_to(lobby: int) -> void:
	if SteamSocial._owner != _social or lobby in [_lobby, _joining]:
		return

	if _old_lobby != 0:
		Steam.leaveLobby(_old_lobby)
	if _joining != 0:
		Steam.leaveLobby(_joining)
	_old_lobby = _lobby
	_old_until = Time.get_ticks_msec() + _MOVE_WAIT_MSEC
	_lobby = 0
	_kicked = 0
	_joining = lobby
	for friend: int in _invitations.keys():
		if _invitations[friend] == lobby:
			_invitations.erase(friend)
	Steam.joinLobby(lobby)
	changed.emit()


func _open() -> void:
	_opening = true
	var serial := _serial
	var made: Array = []
	for attempt in _OPEN_ATTEMPTS:
		made = await _SteamStart.create_lobby(
			Steam.LOBBY_TYPE_INVISIBLE,
			clampi(max_members, 1, 250),
		)
		if made[0] != Steam.RESULT_TIMEOUT or serial != _serial:
			break
	_opening = false
	if serial != _serial or SteamSocial._owner != _social:
		if made[0] == Steam.RESULT_OK:
			Steam.leaveLobby(made[1])
		return
	if made[0] != Steam.RESULT_OK:
		push_warning(
			"Steam did not open the party lobby (%d). " % made[0]
			+ "Check that Steam is online, then invite again.",
		)
		_inviting.clear()
		_lobby = _old_lobby
		_old_lobby = 0
		_kicked = 0
		changed.emit()
		return

	_lobby = made[1]
	Steam.setLobbyData(_lobby, _PARTY_KEY, _PARTY)
	for friend: int in _inviting:
		Steam.inviteUserToLobby(_lobby, friend)
	_inviting.clear()
	changed.emit()


## Invites the friend [param friend] to the party.
func invite(friend: int) -> void:
	if SteamSocial._owner != _social or friend in members:
		return
	if members.size() >= max_members:
		push_warning(
			"The party is full. Raise SteamParty.max_members or kick a "
			+ "member first.",
		)
		return

	if _lobby != 0:
		Steam.inviteUserToLobby(_lobby, friend)
		return
	if friend not in _inviting:
		_inviting.append(friend)
	if not _opening and _joining == 0:
		_open()


## Removes [param member] from the party. The leader moves everyone else to a
## new party and invites them there, and their games accept without asking,
## so the removed member cannot follow. Only the leader can kick.
func kick(member: int) -> void:
	var own := Steam.getSteamID()
	var staying := members
	if SteamSocial._owner != _social or leader != own or member == own \
			or member not in staying or _old_lobby != 0 or _opening:
		return

	staying.erase(own)
	staying.erase(member)
	_old_lobby = _lobby
	_old_until = Time.get_ticks_msec() + _MOVE_WAIT_MSEC
	_lobby = 0
	_kicked = member
	_inviting.assign(staying)
	_open()


## Leaves the party and starts a party of one with the player alone. The
## player also leaves when the [SteamSocial] leaves the tree.
func leave() -> void:
	if SteamSocial._owner != _social:
		return

	for lobby: int in [_lobby, _old_lobby, _joining]:
		if lobby != 0:
			Steam.leaveLobby(lobby)
	_serial += 1
	_lobby = 0
	_old_lobby = 0
	_joining = 0
	_kicked = 0
	_opening = false
	_inviting.clear()
	_followed = ""
	changed.emit()
