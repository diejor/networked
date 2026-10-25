## The player's party of Discord friends with a voice call, which stays
## together across matches and servers.
##
## A signed in player is in a party of one, leading it, until a friend joins.
## Each [DiscordSocial] has one as [member DiscordSocial.party].
## [br][br]
## A Join click on the player's Discord profile, an invitation and a join
## request all join that party.
## [br][br]
## Only the leader can [method kick].
## [br][br]
## Each member follows the leader's server through
## [signal NetwConnection.invited].
## [codeblock]
## func _ready() -> void:
##     discord.party.invited.connect(_on_party_invited)
##
## func _on_friend_activated(index: int) -> void:
##     discord.party.invite(discord.friends[index])
##
## func _on_party_invited(friend: int) -> void:
##     invitation.show_from(discord.profile(friend))
##
## func _on_accept_pressed() -> void:
##     discord.party.join(invitation.friend)
## [/codeblock]
## [b]Voice[/b][br]
## While [member voice_enabled] is on, every member is in the party's call.
## The call keeps going through a match, the end of a session and
## [method NetwSession.transfer_server], and ends when the [DiscordSocial]
## leaves the tree.
class_name DiscordParty
extends RefCounted

## Emitted when [member members], [member leader], [member invitations],
## [member join_requests] or the call change.
signal changed
## Emitted when the friend [param friend] invites the player to their party.
## [method join] accepts it.
signal invited(friend: int)
## Emitted when the friend [param friend] asks to join the player's party.
## [method invite] lets them in.
signal join_requested(friend: int)
## Emitted when [param member] starts or stops speaking in the call.
signal speaking_changed(member: int, speaking: bool)

## How a member is in the party's call, from [method voice_state].
enum VoiceState {
	## Not in the call.
	ABSENT,
	## In the call with the microphone on.
	LISTENING,
	## In the call with the microphone muted.
	MUTED,
	## In the call, hearing nothing and muted.
	DEAFENED,
}

const _LOBBY_PREFIX := "netw-party|"
const _SECRET_MAX_BYTES := 128
const _WRITE_SPACING_MSEC := 2000
const _RETRY_MSEC := 10000
const _MOVE_WAIT_MSEC := 30000
const _QUIT_WAIT_MSEC := 3000
const _ACCEPT_DELAY_SECONDS := 1.0
const _PRESENCE_SPACING_MSEC := 4000

## The most members the party takes. Discord suggests at most 25 people in a
## call.
var max_members := 8:
	set(value):
		max_members = value
		changed.emit()

## Puts every member in the party's voice call.
var voice_enabled := true

## The input action that opens the microphone while pressed. Empty sends the
## player's voice whenever they speak.
var push_to_talk_action: StringName:
	set(value):
		push_to_talk_action = value
		_watched = 0

## Sends the leader's server to [signal NetwConnection.invited] each time
## the leader hosts or joins one, with [member NetwEndpoint.invited_by] set
## to the leader's display name. Keep it on for one party at a time, or two
## leaders pull the player between their servers.
var follow_leader := true

## Mutes the player's microphone in the call. It stays set for the next call.
var muted: bool:
	get:
		return _social.client != null and _social.client.get_self_mute_all()
	set(value):
		if _social.client != null:
			_social.client.set_self_mute_all(value)
			changed.emit()

## Silences the call for the player and mutes their microphone. It stays set
## for the next call.
var deafened: bool:
	get:
		return _social.client != null and _social.client.get_self_deaf_all()
	set(value):
		if _social.client != null:
			_social.client.set_self_deaf_all(value)
			changed.emit()

## The Discord user IDs in the party, the player included, lowest first. A
## member whose game closed or lost Discord is left out until it comes back.
## Empty while signed out.
var members: Array[int]:
	get:
		var own := _social.user_id
		var ids: Array[int] = []
		if own == 0:
			return ids
		ids.append(own)
		var lobby := _handle
		if lobby == null:
			return ids
		for id: int in lobby.lobby_member_ids():
			var member: DiscordLobbyMemberHandle = \
					lobby.get_lobby_member_handle(id)
			if id != own and member != null and member.connected():
				ids.append(id)
		ids.sort()
		return ids

## The friend who started the party while their game is connected, and
## otherwise the member with the lowest user ID. [code]0[/code] while signed
## out.
var leader: int:
	get:
		var present := members
		var lobby := _handle
		var claims: Array[int] = []
		for id: int in present:
			var member: DiscordLobbyMemberHandle = null
			if lobby != null:
				member = lobby.get_lobby_member_handle(id)
			if member != null and member.metadata().get("lead") == "1":
				claims.append(id)
		if not claims.is_empty():
			return claims.min()
		return present.min() if not present.is_empty() else 0

## The friends who invited the player to their party. An invitation leaves
## the list when the player joins that party, when Discord expires it, and
## when the player signs out.
var invitations: Array[int]:
	get:
		var ids: Array[int] = []
		ids.assign(_invitations.keys())
		return ids

## The friends who asked to join the party. A request leaves the list when
## [method invite] answers it, when Discord expires it, and when the player
## signs out.
var join_requests: Array[int]:
	get:
		var ids: Array[int] = []
		ids.assign(_join_requests.keys())
		return ids

## [code]true[/code] while the player is connected to the party's call.
var in_call: bool:
	get:
		var client := _social.client
		if client == null or _lobby == 0:
			return false
		for call: DiscordCall in client.get_calls():
			if call.get_channel_id() == _lobby:
				return call.get_status() == DiscordCallStatus.CONNECTED
		return false

## The secret a friend's Join click on the player's Discord profile joins.
## Empty while signed out.
var join_secret: String:
	get:
		return _secret

static var _owner: DiscordParty
static var _serial: int
static var _presence_at: int
static var _presence_warned: bool
var _social: DiscordSocial
var _presented: Array
var _shown_join: String
var _started_at: int
var _secret: String
var _lobby: int
var _created: bool
var _wants_lobby: bool
var _written: Dictionary[String, String]
var _write_after: int
var _inviting: Array[int]
var _asked: Array[int]
var _invitations: Dictionary[int, DiscordActivityInvite]
var _join_requests: Dictionary[int, DiscordActivityInvite]
var _followed: String
var _old_lobby: int
var _old_until: int
var _kicked: int
var _watched: int
var _call_after: int
var _speaking: Dictionary[int, bool]
var _volumes: Dictionary[int, float]
var _handle: DiscordLobbyHandle:
	get:
		var client := _social.client
		if client == null or _lobby == 0 or _social.user_id == 0:
			return null
		return client.get_lobby_handle(_lobby)


static func _on_rich_presence_updated(
		result: DiscordClientResult,
		serial: int,
		playing_since: float,
		shown_join: String,
) -> void:
	if serial != _serial:
		return
	var party := _owner
	assert(party != null, "A current serial means its party still runs.")
	var player := party._social.profile(party._social.user_id)
	if result.successful():
		player._playing_since = playing_since
		party._shown_join = shown_join
		player.changed.emit()
		return

	player._playing_since = 0.0
	party._shown_join = ""
	party._presented = []
	var ready := party._social.client.get_status() == DiscordClientStatus.READY
	if _presence_warned or not ready:
		return
	_presence_warned = true
	push_warning(
		"Discord did not accept the presence (%s). " % result.error()
		+ "Check the presence properties of DiscordSocial.",
	)


static func _on_activity_join(secret: String) -> void:
	var party := _owner
	if party == null or secret == "p1|" + party._secret:
		return
	if party._social.user_id == 0:
		party._move_to(secret)
		return
	party._social.sign_in().when_settled(party._move_to.bind(secret))


static func _on_activity_invite_created(invite: DiscordActivityInvite) -> void:
	var party := _owner
	var friend := invite.sender_id()
	if party == null or friend == party._social.user_id:
		return

	if invite.type() == DiscordActivityActionTypes.JOIN_REQUEST:
		party._join_requests[friend] = invite
		party.changed.emit()
		party.join_requested.emit(friend)
		return
	if invite.party_id() == ("p1|" + party._secret).sha256_text():
		return
	if friend in party._asked or friend == party.leader:
		party._social.get_tree().create_timer(_ACCEPT_DELAY_SECONDS) \
				.timeout.connect(party._accept.bind(invite, false))
		return
	party._invitations[friend] = invite
	party.changed.emit()
	party.invited.emit(friend)


static func _on_activity_invite_updated(invite: DiscordActivityInvite) -> void:
	var party := _owner
	if party == null or invite.is_valid():
		return
	for received: Dictionary in [party._invitations, party._join_requests]:
		for friend: int in received.keys():
			if received[friend].message_id() == invite.message_id():
				received.erase(friend)
				party.changed.emit()


static func _on_invite_accepted(
		result: DiscordClientResult,
		secret: String,
		invite: DiscordActivityInvite,
		retried: bool,
) -> void:
	var party := _owner
	if party == null:
		return
	if result.successful():
		party._move_to(secret)
		return
	if result.status() == DiscordHttpStatusCode.NOT_FOUND and not retried:
		party._social.get_tree().create_timer(_ACCEPT_DELAY_SECONDS) \
				.timeout.connect(party._accept.bind(invite, true))
		return
	push_warning(
		"Discord could not accept the invitation (%s). " % result.error()
		+ "Ask for a new invitation.",
	)


static func _on_request_sent(result: DiscordClientResult) -> void:
	if not result.successful():
		push_warning(
			"Discord did not send the party invitation or join request "
			+ "(%s). Check that the friend is online, then try again."
			% result.error(),
		)


static func _on_lobby_joined(
		result: DiscordClientResult,
		lobby_id: int,
		secret: String,
) -> void:
	var party := _owner
	if party == null:
		return
	if secret != party._secret:
		if result.successful() \
				and lobby_id not in [party._lobby, party._old_lobby]:
			party._social.client.leave_lobby(
				lobby_id,
				DiscordParty._on_lobby_left,
			)
		return
	if result.successful():
		party._lobby = lobby_id
		party.changed.emit()
		return

	party._written = { }
	var wait := int(result.retry_after() * 1000.0)
	if wait > 0:
		party._write_after = Time.get_ticks_msec() + wait
		return
	push_warning(
		"Discord could not join the party (%s). " % result.error()
		+ "Ask for a new invitation.",
	)
	party.leave()


static func _on_lobby_left(_result: DiscordClientResult) -> void:
	pass


static func _on_lobby_changed(..._details: Array) -> void:
	if _owner != null:
		_owner.changed.emit()


static func _on_call_status(
		status: int,
		_error: int,
		_detail: int,
		lobby_id: int,
) -> void:
	var party := _owner
	if party == null:
		return
	if status == DiscordCallStatus.DISCONNECTED:
		if lobby_id == party._watched:
			party._watched = 0
		for member: int in party._speaking:
			if party._speaking[member]:
				party.speaking_changed.emit(member, false)
		party._speaking.clear()
	party.changed.emit()


static func _on_speaking(member: int, speaking: bool, lobby_id: int) -> void:
	if _owner != null and lobby_id == _owner._watched:
		_owner._speaking[member] = speaking
		_owner.speaking_changed.emit(member, speaking)


static func _on_call_ended() -> void:
	pass


func _init(social: DiscordSocial) -> void:
	_social = social
	changed.connect(_on_changed)
	social.tree_entered.connect(_on_tree_entered)
	social.tree_exiting.connect(_on_tree_exiting)


func _on_tree_entered() -> void:
	var client := _social.client
	if client == null:
		return

	assert(_owner == null, "The party of the last owner ended with it.")
	_owner = self
	client.set_activity_join_callback(DiscordParty._on_activity_join)
	client.set_activity_invite_created_callback(
		DiscordParty._on_activity_invite_created,
	)
	client.set_activity_invite_updated_callback(
		DiscordParty._on_activity_invite_updated,
	)
	client.set_lobby_created_callback(DiscordParty._on_lobby_changed)
	client.set_lobby_updated_callback(DiscordParty._on_lobby_changed)
	client.set_lobby_member_added_callback(DiscordParty._on_lobby_changed)
	client.set_lobby_member_removed_callback(DiscordParty._on_lobby_changed)
	client.set_lobby_member_updated_callback(DiscordParty._on_lobby_changed)
	client.set_voice_participant_changed_callback(
		DiscordParty._on_lobby_changed,
	)
	_social.get_tree().process_frame.connect(_on_process_frame)
	_social.signed_in.connect(_on_signed_in)
	_social.signed_out.connect(_on_signed_out)
	if _social.user_id != 0:
		_on_signed_in(_social.user_id)


func _on_tree_exiting() -> void:
	if _owner != self:
		return

	var client := _social.client
	var own := _social.user_id
	leave()
	_social.get_tree().process_frame.disconnect(_on_process_frame)
	_social.signed_in.disconnect(_on_signed_in)
	_social.signed_out.disconnect(_on_signed_out)

	var until := Time.get_ticks_msec() + _QUIT_WAIT_MSEC
	var held := own != 0 and client.get_status() == DiscordClientStatus.READY
	while held and Time.get_ticks_msec() < until:
		Discord.run_callbacks()
		OS.delay_msec(10)
		held = false
		for id: int in client.get_lobby_ids():
			var handle: DiscordLobbyHandle = client.get_lobby_handle(id)
			var mine: DiscordLobbyMemberHandle = null
			if handle != null:
				mine = handle.get_lobby_member_handle(own)
			held = held or (mine != null \
					and mine.metadata().get("netw") == "party")
	client.clear_rich_presence()
	_serial += 1
	_presented = []
	_shown_join = ""
	_secret = ""
	_owner = null


func _on_signed_in(own: int) -> void:
	var client := _social.client
	for id: int in client.get_lobby_ids():
		var handle: DiscordLobbyHandle = client.get_lobby_handle(id)
		var mine: DiscordLobbyMemberHandle = null
		if handle != null:
			mine = handle.get_lobby_member_handle(own)
		var entry: Dictionary = mine.metadata() if mine != null else { }
		match entry:
			{ "netw": "party", "secret": var secret, .. } when (
					_secret in ["", secret]
			):
				_lobby = id
				_secret = secret
				_created = entry.get("lead") == "1"
				_wants_lobby = true
				_written.assign(entry)
			{ "netw": "party", .. }:
				client.leave_lobby(id, DiscordParty._on_lobby_left)
	if _secret.is_empty():
		leave()
	changed.emit()


func _on_signed_out() -> void:
	_invitations.clear()
	_join_requests.clear()
	_asked.clear()
	leave()


func _on_changed() -> void:
	var own := _social.user_id
	var leading := leader
	var lobby := _handle
	if not follow_leader or own == 0 or leading == own or lobby == null:
		return

	var member: DiscordLobbyMemberHandle = \
			lobby.get_lobby_member_handle(leading)
	var server: String = ""
	if member != null:
		server = member.metadata().get("join", "")
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
	var client := _social.client
	var own := _social.user_id
	var now := Time.get_ticks_msec()
	var ready := client.get_status() == DiscordClientStatus.READY
	var session := Netw.session(_social)
	if not session.is_online or session.local_player == null:
		_started_at = 0
	elif _started_at == 0:
		_started_at = int(Time.get_unix_time_from_system() * 1000.0)

	var secret := "p1|" + _secret
	var size := maxi(members.size(), 1)
	var presence := [
		_social.details,
		_social.state_text,
		_social.large_image,
		_social.large_text,
		_social.small_image,
		_social.small_text,
		_social.public_party,
		_started_at,
		secret,
		size,
		max_members,
	]
	if own != 0 and not ready:
		_presented = []
	elif presence != _presented and now >= _presence_at:
		_presented = presence
		_presence_at = now + _PRESENCE_SPACING_MSEC
		_serial += 1
		var joinable := not _secret.is_empty() and size < max_members
		var activity := DiscordActivity.new()
		activity.set_type(DiscordActivityTypes.PLAYING)
		if not _social.details.is_empty():
			activity.set_details(_social.details)
		if not _social.state_text.is_empty():
			activity.set_state(_social.state_text)
		var assets := DiscordActivityAssets.new()
		if not _social.large_image.is_empty():
			assets.set_large_image(_social.large_image)
		if not _social.large_text.is_empty():
			assets.set_large_text(_social.large_text)
		if not _social.small_image.is_empty():
			assets.set_small_image(_social.small_image)
		if not _social.small_text.is_empty():
			assets.set_small_text(_social.small_text)
		if not (_social.large_image + _social.large_text + _social.small_image
				+ _social.small_text).is_empty():
			activity.set_assets(assets)
		if _started_at != 0:
			var timestamps := DiscordActivityTimestamps.new()
			timestamps.set_start(_started_at)
			activity.set_timestamps(timestamps)
		var shown_party := DiscordActivityParty.new()
		shown_party.set_id(secret.sha256_text())
		shown_party.set_current_size(size)
		shown_party.set_max_size(clampi(max_members, 0, 0x7fffffff))
		shown_party.set_privacy(
			DiscordActivityPartyPrivacy.PUBLIC if _social.public_party
			else DiscordActivityPartyPrivacy.PRIVATE,
		)
		activity.set_party(shown_party)
		if joinable:
			var secrets := DiscordActivitySecrets.new()
			secrets.set_join(secret)
			activity.set_secrets(secrets)
			activity.set_supported_platforms(
				DiscordActivityGamePlatforms.DESKTOP,
			)
		client.update_rich_presence(
			activity,
			DiscordParty._on_rich_presence_updated.bind(
				_serial,
				_started_at / 1000.0,
				secret if joinable else "",
			),
		)

	if own == 0 or not ready:
		return

	if (_wants_lobby or _social.public_party) and now >= _write_after:
		_write_after = now + _WRITE_SPACING_MSEC
		var entry: Dictionary[String, String] = {
			"netw": "party",
			"secret": _secret,
		}
		if _created:
			entry.lead = "1"
		var endpoint := session.connection.current_endpoint
		var address := session.connection.join_address
		if leader == own and session.is_online \
				and session.local_player != null and endpoint != null \
				and not endpoint.peer_class.is_empty() \
				and not address.is_empty():
			entry.join = "%s|%s" % [endpoint.peer_class, address]
		if entry != _written:
			_written = entry
			var lobby_map: Dictionary[String, String] = { }
			client.create_or_join_lobby_with_metadata(
				_LOBBY_PREFIX + _secret,
				lobby_map,
				entry,
				DiscordParty._on_lobby_joined.bind(_secret),
			)

	var call: DiscordCall = null
	for found: DiscordCall in client.get_calls():
		if _lobby != 0 and found.get_channel_id() == _lobby:
			call = found
	var want := voice_enabled and _lobby != 0
	if want and call == null and now >= _call_after:
		_call_after = now + _RETRY_MSEC
		client.start_call(_lobby)
	elif want and call != null and _watched != _lobby:
		_watched = _lobby
		call.set_status_changed_callback(
			DiscordParty._on_call_status.bind(_lobby),
		)
		call.set_participant_changed_callback(DiscordParty._on_lobby_changed)
		call.set_speaking_status_changed_callback(
			DiscordParty._on_speaking.bind(_lobby),
		)
		call.set_on_voice_state_changed_callback(
			DiscordParty._on_lobby_changed,
		)
		call.set_audio_mode(
			DiscordAudioModeType.MODE_VAD if push_to_talk_action.is_empty()
			else DiscordAudioModeType.MODE_PTT,
		)
		call.set_ptt_active(false)
		for member: int in _volumes:
			call.set_participant_volume(member, _volumes[member])
	elif not voice_enabled and _watched != 0:
		client.end_call(_watched, DiscordParty._on_call_ended)
		_watched = 0
	if call != null and InputMap.has_action(push_to_talk_action):
		var down := Input.is_action_pressed(push_to_talk_action)
		if down != call.get_ptt_active():
			call.set_ptt_active(down)

	if not _inviting.is_empty() and _shown_join == secret:
		for friend: int in _inviting:
			client.send_activity_invite(
				friend,
				"",
				DiscordParty._on_request_sent,
			)
		_inviting.clear()

	if _old_lobby == 0:
		return
	var moved := true
	var old: DiscordLobbyHandle = client.get_lobby_handle(_old_lobby)
	if _kicked != 0 and old != null:
		var arrived := members
		for id: int in old.lobby_member_ids():
			var member: DiscordLobbyMemberHandle = \
					old.get_lobby_member_handle(id)
			moved = moved and (id in [own, _kicked] or id in arrived \
					or member == null or not member.connected())
	if now >= _old_until \
			or (moved and _lobby != 0 and (in_call or not voice_enabled)):
		client.end_call(_old_lobby, DiscordParty._on_call_ended)
		client.leave_lobby(_old_lobby, DiscordParty._on_lobby_left)
		_old_lobby = 0
		_kicked = 0


func _move_to(secret: String) -> void:
	var hex := secret.trim_prefix("p1|")
	if not secret.begins_with("p1|") or hex.is_empty() \
			or secret.to_utf8_buffer().size() > _SECRET_MAX_BYTES:
		push_warning(
			"The Discord invitation is not from this version of the game. "
			+ "Ask for a new invitation.",
		)
		return
	if hex == _secret:
		return

	var client := _social.client
	if _old_lobby != 0 and _social.user_id != 0:
		client.end_call(_old_lobby, DiscordParty._on_call_ended)
		client.leave_lobby(_old_lobby, DiscordParty._on_lobby_left)
	_old_lobby = _lobby
	_old_until = Time.get_ticks_msec() + _MOVE_WAIT_MSEC
	_lobby = 0
	_secret = hex
	_created = false
	_kicked = 0
	_wants_lobby = true
	_written = { }
	_asked.clear()
	for friend: int in _invitations.keys():
		if _invitations[friend].party_id() == secret.sha256_text():
			_invitations.erase(friend)
	changed.emit()


func _accept(invite: DiscordActivityInvite, retried: bool) -> void:
	if _owner != self or _social.user_id == 0:
		return
	await _social.sign_in().wait()
	if _owner == self and _social.user_id != 0:
		_social.client.accept_activity_invite(
			invite,
			DiscordParty._on_invite_accepted.bind(invite, retried),
		)


## Invites the friend [param friend] to the party, or lets them in when they
## asked with a join request. Does nothing while the player is signed out.
func invite(friend: int) -> void:
	if _owner != self or _social.user_id == 0 or friend in members:
		return
	if members.size() >= max_members:
		push_warning(
			"The party is full. Raise DiscordParty.max_members or kick a "
			+ "member first.",
		)
		return

	_wants_lobby = true
	if _join_requests.has(friend):
		_social.client.send_activity_join_request_reply(
			_join_requests[friend],
			DiscordParty._on_request_sent,
		)
		_join_requests.erase(friend)
		changed.emit()
	elif friend not in _inviting:
		_inviting.append(friend)


## Joins the party of the friend [param friend]. It accepts their invitation
## in [member invitations], or else sends them a join request that they
## answer with [method invite].
func join(friend: int) -> void:
	if _owner != self or _social.user_id == 0 or friend in members:
		return

	if _invitations.has(friend):
		_accept(_invitations[friend], false)
		return
	if friend not in _asked:
		_asked.append(friend)
	_social.client.send_activity_join_request(
		friend,
		DiscordParty._on_request_sent,
	)


## Removes [param member] from the party. The leader moves everyone else to a
## new party and invites them there, and their games accept without asking,
## so the removed member cannot follow. Only the leader can kick.
func kick(member: int) -> void:
	var own := _social.user_id
	var staying := members
	if _owner != self or own == 0 or leader != own or member == own \
			or member not in staying or _old_lobby != 0:
		return

	_move_to("p1|" + Crypto.new().generate_random_bytes(32).hex_encode())
	_created = true
	_kicked = member
	staying.erase(own)
	staying.erase(member)
	_inviting.append_array(staying)


## Leaves the party and its call, and starts a party of one with the player
## alone. The player also leaves when the [DiscordSocial] leaves the tree.
func leave() -> void:
	if _owner != self:
		return

	var client := _social.client
	for lobby: int in [_lobby, _old_lobby]:
		if lobby != 0 and _social.user_id != 0:
			client.end_call(lobby, DiscordParty._on_call_ended)
			client.leave_lobby(lobby, DiscordParty._on_lobby_left)
	_secret = ""
	if _social.user_id != 0:
		_secret = Crypto.new().generate_random_bytes(32).hex_encode()
	_lobby = 0
	_old_lobby = 0
	_kicked = 0
	_created = true
	_wants_lobby = false
	_written = { }
	_inviting.clear()
	_followed = ""
	changed.emit()


## Returns how [param member] is in the party's call. It answers whether or
## not the player is in the call.
func voice_state(member: int) -> VoiceState:
	var lobby := _handle
	var call: DiscordCallInfoHandle = null
	if lobby != null:
		call = lobby.get_call_info_handle()
	if call == null or member not in call.get_participants():
		return VoiceState.ABSENT
	var voice: DiscordVoiceStateHandle = call.get_voice_state_handle(member)
	if voice != null and voice.self_deaf():
		return VoiceState.DEAFENED
	if voice != null and voice.self_mute():
		return VoiceState.MUTED
	return VoiceState.LISTENING


## Returns [code]true[/code] while [param member] is speaking in the call.
func is_speaking(member: int) -> bool:
	return _speaking.get(member, false)


## Sets how loud the player hears [param member], from [code]0[/code] to
## [code]200[/code], where [code]100[/code] is normal. It lasts as long as
## the [DiscordSocial].
func set_volume(member: int, percent: float) -> void:
	_volumes[member] = clampf(percent, 0.0, 200.0)
	var client := _social.client
	if client == null or _lobby == 0:
		return
	for call: DiscordCall in client.get_calls():
		if call.get_channel_id() == _lobby:
			call.set_participant_volume(member, _volumes[member])
