## The player's party of Nakama friends, which stays together across matches
## and servers.
##
## A signed in player is in a party of one, leading it, until a friend joins.
## Each [NakamaAccount] has one as [member NakamaAccount.party].
## [br][br]
## An invitation and a join request both reach a friend through
## [code]server/networked.js[/code], which the Nakama server must run.
## [br][br]
## Only the leader invites, kicks and queues for a match, because Nakama sends
## every join request to the leader.
## [br][br]
## Each member follows the leader's server through
## [signal NetwConnection.invited], and [method find_match] queues the whole
## party in Nakama's matchmaker.
## [codeblock]
## @onready var nakama: NakamaAccount = session.service(NakamaAccount)
##
## func _ready() -> void:
##     nakama.party.invited.connect(_on_party_invited)
##     session.connection.invited.connect(_on_invited)
##
## func _on_invite_pressed(user_id: String) -> void:
##     nakama.party.invite(user_id)
##
## func _on_party_invited(friend: String) -> void:
##     invitation.show_from(friend)
##
## func _on_accept_pressed() -> void:
##     nakama.party.join(invitation.friend)
##
## func _on_find_match_pressed() -> void:
##     nakama.party.find_match(2, 8)
##
## func _on_invited(endpoint: NetwEndpoint) -> void:
##     await session.connection.join(endpoint).settled
## [/codeblock]
class_name NakamaParty
extends RefCounted

## Emitted when [member members], [member leader], [member invitations] or
## [member join_requests] change.
signal changed
## Emitted when the user [param friend] invites the player to their party.
## [method join] accepts it.
signal invited(friend: String)
## Emitted when the user [param friend] asks to join the player's party.
## [method invite] lets them in.
signal join_requested(friend: String)

const _INVITE_RPC := "networked_party_invite"
const _REQUEST_RPC := "networked_party_request"
const _INVITE_CODE := 1101
const _REQUEST_CODE := 1102
const _SERVER_OP := 1
const _MATCHED := "matched:"

## The most members the party takes.
var max_members := 8:
	set(value):
		max_members = value
		changed.emit()

## Sends the leader's server to [signal NetwConnection.invited] each time
## the leader hosts or joins one, with [member NetwEndpoint.invited_by] set
## to the leader's display name. Keep it on for one party at a time, or two
## leaders pull the player between their servers.
var follow_leader := true

## The Nakama user IDs in the party, the player included, lowest first.
## Empty while signed out.
var members: Array[String]:
	get:
		var ids: Array[String] = []
		if _account.user_id.is_empty():
			return ids
		if _party_id.is_empty():
			ids.append(_account.user_id)
			return ids
		ids.assign(_presences.keys())
		ids.sort()
		return ids

## The member who leads the party. Nakama picks another when the leader
## leaves. Empty while signed out.
var leader: String:
	get:
		return _account.user_id if _party_id.is_empty() else _leader_id

## The users who invited the player to their party. An invitation leaves the
## list when the player joins that party and when the player signs out.
var invitations: Array[String]:
	get:
		var ids: Array[String] = []
		ids.assign(_invitations.keys())
		return ids

## The users who asked to join the party. A request leaves the list when
## [method invite] answers it and when the player signs out.
var join_requests: Array[String]:
	get:
		var ids: Array[String] = []
		ids.assign(_join_requests.keys())
		return ids

var _account: NakamaAccount
var _socket: NakamaSocket
var _party_id := ""
var _leader_id := ""
var _presences: Dictionary[String, NakamaRTAPI.UserPresence]
var _invitations: Dictionary[String, String]
var _join_requests: Dictionary[String, NakamaRTAPI.UserPresence]
var _inviting: Array[String]
var _asked: Array[String]
var _creating := false
var _ticket := ""
var _written := ""
var _followed := ""
var _warned_module := false


func _init(account: NakamaAccount) -> void:
	_account = account
	account.signed_in.connect(_on_signed_in)
	account.signed_out.connect(_on_signed_out)


func _on_signed_in() -> void:
	if _socket != null and _socket.received_party.is_connected(_on_received_party):
		_socket.received_party.disconnect(_on_received_party)
		_socket.received_party_presence.disconnect(_on_received_party_presence)
		_socket.received_party_leader.disconnect(_on_received_party_leader)
		_socket.received_party_close.disconnect(_on_received_party_close)
		_socket.received_party_data.disconnect(_on_received_party_data)
		_socket.received_party_join_request.disconnect(
			_on_received_party_join_request,
		)
		_socket.received_party_matchmaker_ticket.disconnect(
			_on_received_party_matchmaker_ticket,
		)
		_socket.received_matchmaker_matched.disconnect(
			_on_received_matchmaker_matched,
		)
		_socket.received_notification.disconnect(_on_received_notification)
	_socket = _account.socket
	_socket.received_party.connect(_on_received_party)
	_socket.received_party_presence.connect(_on_received_party_presence)
	_socket.received_party_leader.connect(_on_received_party_leader)
	_socket.received_party_close.connect(_on_received_party_close)
	_socket.received_party_data.connect(_on_received_party_data)
	_socket.received_party_join_request.connect(_on_received_party_join_request)
	_socket.received_party_matchmaker_ticket.connect(
		_on_received_party_matchmaker_ticket,
	)
	_socket.received_matchmaker_matched.connect(_on_received_matchmaker_matched)
	_socket.received_notification.connect(_on_received_notification)
	var frames := _account.get_tree().process_frame
	if not frames.is_connected(_on_process_frame):
		frames.connect(_on_process_frame)
	_reset()


func _on_signed_out() -> void:
	_invitations.clear()
	_join_requests.clear()
	_inviting.clear()
	_asked.clear()
	_reset()


func _on_process_frame() -> void:
	if not _account.is_inside_tree() or _party_id.is_empty() \
			or _leader_id != _account.user_id:
		return

	var connection := Netw.session(_account).connection
	var server := ""
	if Netw.session(_account).is_online \
			and connection.current_endpoint != null \
			and not connection.join_address.is_empty():
		server = "%s|%s" % [
			connection.current_endpoint.peer_class,
			connection.join_address,
		]
	if server != _written:
		_written = server
		if not server.is_empty():
			_socket.send_party_data_async(_party_id, _SERVER_OP, server)


func _on_received_party(party: NakamaRTAPI.Party) -> void:
	_take(party)
	for friend: String in _invitations.keys():
		if _invitations[friend] == party.party_id:
			_invitations.erase(friend)
	changed.emit()


func _on_received_party_presence(event: NakamaRTAPI.PartyPresenceEvent) -> void:
	if event.party_id != _party_id:
		return
	for presence in event.joins:
		_presences[presence.user_id] = presence
		_join_requests.erase(presence.user_id)
		_written = ""
	for presence in event.leaves:
		_presences.erase(presence.user_id)
	changed.emit()


func _on_received_party_leader(event: NakamaRTAPI.PartyLeader) -> void:
	if event.party_id == _party_id:
		_leader_id = event.presence.user_id
		_written = ""
		changed.emit()


func _on_received_party_close(event: NakamaRTAPI.PartyClose) -> void:
	if event.party_id == _party_id:
		_reset()


func _on_received_party_data(data: NakamaRTAPI.PartyData) -> void:
	var session := Netw.session(_account)
	if data.party_id != _party_id or data.op_code != _SERVER_OP \
			or data.presence == null or data.presence.user_id != _leader_id \
			or not follow_leader or data.data == _followed:
		return
	_followed = data.data
	match Array(data.data.split("|", true, 1)):
		[var peer_class, var address] when (
				not peer_class.is_empty()
				and not address.is_empty()
				and address != session.connection.join_address
		):
			_invite_to(StringName(peer_class), address)


func _on_received_party_join_request(request: NakamaRTAPI.PartyJoinRequest) -> void:
	if request.party_id != _party_id:
		return
	for presence in request.presences:
		if presence.user_id in _inviting:
			_inviting.erase(presence.user_id)
			_socket.accept_party_member_async(_party_id, presence)
			continue
		_join_requests[presence.user_id] = presence
		changed.emit()
		join_requested.emit(presence.user_id)


func _on_received_party_matchmaker_ticket(
		ticket: NakamaRTAPI.PartyMatchmakerTicket,
) -> void:
	if ticket.party_id == _party_id:
		_ticket = ticket.ticket


func _on_received_matchmaker_matched(
		matched: NakamaRTAPI.MatchmakerMatched,
) -> void:
	if _ticket.is_empty() or matched.ticket != _ticket:
		return
	_ticket = ""
	var sessions := PackedStringArray()
	for user in matched.users:
		sessions.append(user.presence.session_id)
	var packed := JSON.stringify(
		{
			"ticket": matched.ticket,
			"token": matched.token,
			"match_id": matched.match_id,
			"sessions": sessions,
		},
	)
	_invite_to(&"NakamaRelayPeer", _MATCHED + packed)


func _on_received_notification(notification: NakamaAPI.ApiNotification) -> void:
	var friend := notification.sender_id
	match [notification.code, JSON.parse_string(notification.content)]:
		[_INVITE_CODE, { "party_id": var party_id, .. }] when party_id is String:
			_invitations[friend] = party_id
			if friend in _asked:
				_asked.erase(friend)
				join(friend)
				return
			changed.emit()
			invited.emit(friend)
		[_REQUEST_CODE, _]:
			if friend in members:
				return
			_join_requests[friend] = null
			changed.emit()
			join_requested.emit(friend)


func _take(party: NakamaRTAPI.Party) -> void:
	_party_id = party.party_id
	_leader_id = party.leader.user_id
	_presences.clear()
	_presences[party.self_presence.user_id] = party.self_presence
	for presence in party.presences:
		_presences[presence.user_id] = presence
	_written = ""


func _reset() -> void:
	_party_id = ""
	_leader_id = ""
	_presences.clear()
	_ticket = ""
	_written = ""
	_followed = ""
	changed.emit()


func _invite_to(peer_class: StringName, address: String) -> void:
	var leading := _leader_id
	var shown := ""
	if _presences.has(leading):
		shown = _presences[leading].username
	var found = await _account.client.get_users_async(
		_account.session,
		PackedStringArray([leading]),
	)
	if not found.is_exception() and not found.users.is_empty() \
			and not found.users[0].display_name.is_empty():
		shown = found.users[0].display_name
	Netw.session(_account).connection.invite(peer_class, address, shown)


func _call(rpc: String, payload: Dictionary) -> bool:
	var answer = await _account.client.rpc_async(
		_account.session,
		rpc,
		JSON.stringify(payload),
	)
	if not answer.is_exception():
		return true
	if not _warned_module:
		_warned_module = true
		push_warning(
			"The Nakama server refused %s (%s). " % [
				rpc,
				answer.get_exception().message,
			]
			+ "Install addons/networked_nakama/server/networked.js on the "
			+ "server.",
		)
	return false


func _ensure_party() -> bool:
	while _creating:
		await changed
	if not _party_id.is_empty():
		return true
	_creating = true
	var made: NakamaRTAPI.Party = await _socket.create_party_async(
		false,
		max_members,
	)
	_creating = false
	if made.is_exception():
		push_warning(
			"Nakama did not create the party (%s). Try again."
			% made.get_exception().message,
		)
		changed.emit()
		return false
	_take(made)
	changed.emit()
	return true


func _send_invite(friend: String) -> void:
	if await _ensure_party():
		await _call(_INVITE_RPC, { "user_id": friend, "party_id": _party_id })


func _enter(party_id: String) -> void:
	if not _party_id.is_empty():
		await _socket.leave_party_async(_party_id)
		_reset()
	var joined = await _socket.join_party_async(party_id)
	if joined.is_exception():
		push_warning(
			"Nakama did not let the player join the party (%s). "
			% joined.get_exception().message
			+ "Ask for a new invitation.",
		)
		_invitations.erase(_invitations.find_key(party_id))
		changed.emit()


func _queue(minimum: int, maximum: int, query: String) -> void:
	if not await _ensure_party():
		return
	var ticket = await _socket.add_matchmaker_party_async(
		_party_id,
		query,
		minimum,
		maximum,
	)
	if ticket.is_exception():
		push_warning(
			"Nakama did not queue the party (%s). Check the minimum and "
			% ticket.get_exception().message
			+ "maximum counts.",
		)
		return
	_ticket = ticket.ticket


## Invites the user [param friend] to the party, or lets them in when they
## asked to join. Only the leader invites. Does nothing while the player is
## signed out.
func invite(friend: String) -> void:
	var own := _account.user_id
	if own.is_empty() or friend in members:
		return
	if leader != own:
		push_warning(
			"Only the party leader can invite. Ask the leader, or leave the "
			+ "party first.",
		)
		return
	if members.size() >= max_members:
		push_warning(
			"The party is full. Raise NakamaParty.max_members or kick a member "
			+ "first.",
		)
		return

	var asking: NakamaRTAPI.UserPresence = _join_requests.get(friend)
	_join_requests.erase(friend)
	changed.emit()
	if asking != null:
		_socket.accept_party_member_async(_party_id, asking)
		return
	if friend not in _inviting:
		_inviting.append(friend)
	_send_invite(friend)


## Joins the party of the user [param friend]. It accepts their invitation in
## [member invitations], or else asks them to join, which they answer with
## [method invite].
func join(friend: String) -> void:
	if _account.user_id.is_empty() or friend in members:
		return
	if _invitations.has(friend):
		_enter(_invitations[friend])
		return
	if friend not in _asked:
		_asked.append(friend)
	_call(_REQUEST_RPC, { "user_id": friend })


## Removes [param member] from the party, and the removed member's party
## ends as if they had left. Only the leader can kick.
func kick(member: String) -> void:
	var own := _account.user_id
	if own.is_empty() or leader != own or member == own \
			or not _presences.has(member):
		return
	_inviting.erase(member)
	_socket.remove_party_member_async(_party_id, _presences[member])


## Leaves the party and starts a party of one with the player alone. The
## player also leaves when the [NakamaAccount] signs out.
func leave() -> void:
	if not _party_id.is_empty() and _socket != null:
		_socket.leave_party_async(_party_id)
	_reset()


## Queues the whole party in Nakama's matchmaker for a match of
## [param minimum] to [param maximum] players that fits [param query]. The
## match reaches every member at [signal NetwConnection.invited]. Only the
## leader can queue.
func find_match(minimum: int, maximum: int, query := "*") -> void:
	if _account.user_id.is_empty() or leader != _account.user_id:
		return
	_queue(minimum, maximum, query)
