## A [LobbyDirectory] that hosts and joins Nakama relay matches as the
## session's [NakamaAccount].
##
## The host needs no open port, so a web export can host. A lobby's address
## is its match ID, which players can share to join directly.
## [br][br]
## Every player arrives with [member NetwPlayer.account]
## [code]"nakama:<user id>"[/code] and their Nakama display name. Nakama shows
## every match participant every other user ID.
## [br][br]
## An address starting [code]room:[/code] names a room that every player joins.
## The first player to arrive serves it. When its server is lost, a player
## still in it serves with [member migrate_on_host_loss] on. Otherwise, and
## after the server calls [method NetwSession.leave], the next player to
## arrive does. A room is never listed.
## [codeblock]
## var room := session.connection.add(&"NakamaRelayPeer", "room:" + party_id)
## await session.connection.join(room).settled
## [/codeblock]
class_name NakamaLobbyDirectory
extends LobbyDirectory

const _Async := preload("res://addons/networked/gdscript/async.gd")
const _Bridge := preload("nakama_relay_bridge.gd")
const _LOBBY_COLLECTION := "lobbies"
const _MATCHED := "matched:"
const _ROOM := "room:"

## Seconds to wait for a match to fully join before failing.
@export_range(1.0, 30.0, 0.5, "suffix:s") var connect_timeout: float = 10.0

## The player limit shown in the lobby list, when the host sets none.
@export_range(1, 250, 1, "or_greater", "suffix:players") var max_clients: int = 8

## Only lobbies with the same tag are listed, so games sharing a Nakama server
## do not see each other's lobbies.
@export var browser_filter_uid: String = "networked"

## When the host is lost or closes its window, the players left choose a new
## server among themselves and keep the match. [method NetwSession.leave]
## still ends every player's session, so a host that wants the match to go
## on calls [method NetwSession.transfer_server] first.
@export var migrate_on_host_loss := false:
	set(value):
		migrate_on_host_loss = value
		if _bridge != null:
			_bridge.migrate_on_host_loss = value


## The listing of one lobby, as stored in Nakama.
class LobbyCard:
	extends Resource

	## Relay match id this card describes.
	@export var match_id: String = ""

	## Advertised lobby name.
	@export var lobby_name: String = ""

	## Host display name.
	@export var host: String = ""

	## The host's [member NetwSessionConfig.app_id].
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


var _account: NakamaAccount
var _bridge: _Bridge
var _hosted_match_id: String = ""
var _room: String = ""
var _names: Dictionary[String, String]
var _warned_unconfigured := false


func _init() -> void:
	display_name = "Nakama"
	address_label = "Match ID"
	address_help = "Paste the Nakama match id shared by the host."
	can_host_here = true
	capabilities = (
			LobbyDirectory.CAPABILITY_BROWSE
			| LobbyDirectory.CAPABILITY_FRIEND_NAMES
	)


func _service_exiting(_session: NetwSession) -> void:
	if not _hosted_match_id.is_empty():
		_delete_card()
	_leave_match()


## A [constant NetwServerInfo.VISIBILITY_PRIVATE] lobby is not listed, and can
## only be joined by its match id.
func _host_lobby(settings: Dictionary) -> void:
	if not await _signed_in():
		fail(ERR_UNAVAILABLE, "the Nakama server is unreachable.")
		return
	_new_bridge()
	_bridge.create_match()
	var peer := await _await_match("_host_lobby")
	if peer == null:
		fail(ERR_CANT_CREATE, "the relay produced no match peer.")
		return

	var visibility := int(
		settings.get("visibility", NetwServerInfo.VISIBILITY_PUBLIC),
	) as NetwServerInfo.Visibility
	if visibility == NetwServerInfo.VISIBILITY_FRIENDS_ONLY:
		push_warning(
			"Nakama has no friends-only lobbies, so this lobby is private. "
			+ "Use VISIBILITY_PRIVATE or VISIBILITY_PUBLIC.",
		)
	if visibility == NetwServerInfo.VISIBILITY_PUBLIC:
		var card := LobbyCard.new()
		card.match_id = _bridge.match_id
		card.lobby_name = String(settings.get("name", ""))
		card.host = _account.display_name
		card.app_id = String(Netw.session(self).config.app_id)
		card.uid = browser_filter_uid
		var wanted := int(settings.get("max_players", 0))
		card.max_players = wanted if wanted > 0 else max_clients
		card.visibility = visibility
		var written = await _account.client.write_storage_objects_async(
			_account.session,
			[
				NakamaWriteStorageObject.new(
					_LOBBY_COLLECTION,
					card.match_id,
					2,
					1,
					JSON.stringify(card.to_dict()),
					"",
				),
			],
		)
		if written.is_exception():
			push_warning(
				"The lobby could not be listed on the Nakama server, so other "
				+ "players will not see it in the browser.",
			)
		else:
			_hosted_match_id = card.match_id
	deliver(peer)


func _join_lobby(address: String) -> void:
	if address.is_empty():
		fail(ERR_INVALID_PARAMETER, "the match id is empty.")
		return
	if not await _signed_in():
		fail(ERR_UNAVAILABLE, "the Nakama server is unreachable.")
		return
	var users: Array = []
	var matched: NakamaRTAPI.MatchmakerMatched = null
	var shape: Variant = null
	if address.begins_with(_MATCHED):
		shape = JSON.parse_string(address.trim_prefix(_MATCHED))
	match shape:
		{
			"ticket": var ticket,
			"token": var token,
			"match_id": var match_id,
			"sessions": var sessions,
		} when ticket is String and token is String and sessions is Array:
			for session_id in sessions:
				users.append(
					{ "presence": { "session_id": session_id, "user_id": "" } },
				)
			matched = NakamaRTAPI.MatchmakerMatched.create(
				NakamaRTAPI,
				{
					"ticket": ticket,
					"token": token,
					"match_id": match_id,
					"users": users,
					"self": { "presence": { "session_id": "", "user_id": "" } },
				},
			)
	if address.begins_with(_MATCHED) and matched == null:
		fail(ERR_INVALID_PARAMETER, "the matchmaker address is damaged.")
		return

	_new_bridge()
	if address.begins_with(_ROOM):
		_room = address
		_bridge.join_named_match(address.trim_prefix(_ROOM))
	elif matched == null:
		_bridge.join_match(address)
	else:
		_bridge.join_matched(matched)
	var peer := await _await_match("_join_lobby")
	if peer == null:
		fail(ERR_CANT_CONNECT, "the relay produced no match peer.")
		return
	deliver(peer)


func _list_lobbies() -> void:
	var addresses := PackedStringArray()
	var names := PackedStringArray()
	var infos: Array[NetwServerInfo] = []
	if not await _signed_in():
		publish_lobbies(addresses, names, infos)
		return

	var cards = await _account.client.list_storage_objects_async(
		_account.session,
		_LOBBY_COLLECTION,
		"",
		100,
	)
	var matches = await _account.client.list_matches_async(
		_account.session,
		0,
		100,
		100,
		false,
		"",
		"",
	)
	if cards.is_exception() or matches.is_exception():
		publish_lobbies(addresses, names, infos)
		return

	var live_sizes: Dictionary[String, int] = { }
	for listed in matches.matches:
		live_sizes[listed.match_id] = listed.size
	for object in cards.objects:
		var stored: Variant = JSON.parse_string(object.value)
		if not live_sizes.has(object.key) or not stored is Dictionary:
			continue
		var card := LobbyCard.from_dict(object.key, stored)
		if card.uid != browser_filter_uid:
			continue
		addresses.append(card.match_id)
		names.append(card.lobby_name)
		infos.append(card.to_server_info(live_sizes[card.match_id]))
	publish_lobbies(addresses, names, infos)


func _leave_lobby() -> void:
	if not _hosted_match_id.is_empty():
		await _delete_card()
	_leave_match()


func _peer_class() -> StringName:
	return &"NakamaRelayPeer"


func _join_address() -> String:
	if _bridge == null:
		return ""
	return _room if not _room.is_empty() else _bridge.match_id


func _local_member_name() -> String:
	if _account == null:
		return LobbyDirectory.local_member_name_default()
	return _account.display_name


func _member_name(peer_id: int) -> String:
	var presence = null
	if _bridge != null:
		presence = _bridge.get_user_presence_for_peer(peer_id)
	if presence == null:
		return LobbyDirectory.member_name_default(peer_id)
	return _names.get(presence.user_id, presence.username)


func _account_of(peer_id: int) -> String:
	var presence = null
	if _bridge != null:
		presence = _bridge.get_user_presence_for_peer(peer_id)
	return "nakama:" + presence.user_id if presence != null else ""


func _signed_in() -> bool:
	_account = Netw.session(self).service(NakamaAccount) as NakamaAccount
	if _account == null:
		if not _warned_unconfigured:
			push_error(
				"NakamaLobbyDirectory has no NakamaAccount to sign in with. "
				+ "Add a NakamaAccount node to the session.",
			)
			_warned_unconfigured = true
		provider_unavailable.emit("The session has no NakamaAccount.")
		return false

	var signing_in := _account.sign_in()
	await signing_in.wait()
	if signing_in.is_failed:
		push_warning(signing_in.detail)
		provider_unavailable.emit(signing_in.detail)
		return false
	return true


func _new_bridge() -> void:
	_bridge = _Bridge.new(_account.socket)
	_bridge.migrate_on_host_loss = migrate_on_host_loss
	_bridge.match_join_error.connect(_on_match_join_error)
	_names[_account.user_id] = _account.display_name
	if not _account.socket.received_match_presence.is_connected(
		_on_received_match_presence,
	):
		_account.socket.received_match_presence.connect(
			_on_received_match_presence,
		)


func _await_match(op: String) -> MultiplayerPeer:
	var timer := get_tree().create_timer(connect_timeout)
	var outcome := await _Async.timeout_or_failure(
		_bridge.match_joined,
		_bridge.match_join_error,
		timer,
	)
	if outcome.result != "success":
		push_error(
			(
					"NakamaLobbyDirectory.%s did not join a match (%s). Check the "
					+ "server, or raise connect_timeout."
			) % [op, outcome.result],
		)
		_leave_match()
		return null
	return _bridge.multiplayer_peer


func _leave_match() -> void:
	if _bridge == null:
		return
	var socket: NakamaSocket = _bridge.nakama_socket
	if not _bridge.match_id.is_empty() and socket.is_connected_to_host():
		socket.leave_match_async(_bridge.match_id)
	_bridge.leave()
	_bridge = null
	_room = ""


func _delete_card() -> void:
	var match_id := _hosted_match_id
	_hosted_match_id = ""
	await _account.client.delete_storage_objects_async(
		_account.session,
		[NakamaStorageObjectId.new(_LOBBY_COLLECTION, match_id, "", "")],
	)


func _on_match_join_error(exception: Variant) -> void:
	push_warning(
		"Joining the Nakama match failed (%s)."
		% (exception.message if exception != null else "no reason given"),
	)


func _on_received_match_presence(event: Variant) -> void:
	if _bridge == null or event.match_id != _bridge.match_id:
		return
	var asked := PackedStringArray()
	for presence in event.joins:
		if not _names.has(presence.user_id):
			asked.append(presence.user_id)
	if asked.is_empty():
		return
	var found = await _account.client.get_users_async(_account.session, asked)
	if found.is_exception():
		return
	for user in found.users:
		var shown: String = user.display_name
		_names[user.id] = user.username if shown.is_empty() else shown
