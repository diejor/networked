## Proves the player's Steam account with a Steam session ticket.
##
## [codeblock]
## func _init() -> void:
##     Netw.configure_admission(admit).accept(SteamTicket.new())
##
## func admit(request: NetwJoinRequest) -> Error:
##     return OK if request.account.begins_with("steam:") else ERR_UNAUTHORIZED
## [/codeblock]
## The server must run Steam to check a ticket.
## [br][br]
## Over ENet or WebRTC [method NetwJoinRequest.is_bound] is [code]false[/code]
## for the account.
class_name SteamTicket
extends NetwAccountProvider

const _SteamStart := preload("steam_start.gd")
const _APP_ID_SETTING := "steam/initialization/app_id"
const _TICKET_ISSUED := 1
const _TICKET_VALID := 0

var _warned := false
var _tickets: Dictionary[int, NetwSession]
var _begun: Dictionary[Array, int]


func _prove(_challenge: String, session: NetwSession) -> Variant:
	if _SteamStart.start(ProjectSettings.get_setting(_APP_ID_SETTING, 0)) \
			.status != 0:
		return null

	var steam := Engine.get_singleton(&"Steam")
	var peer := session.root.multiplayer.multiplayer_peer
	var host := 0
	if peer.is_class("SteamMultiplayerPeer"):
		host = peer.call(&"get_steam_id_for_peer_id", 1)
	var made: Dictionary = steam.getAuthSessionTicket(host)
	var handle: int = made.get("id", 0)
	if handle == 0:
		return null

	var issued: Array = await steam.get_auth_session_ticket_response
	while issued[0] != handle:
		issued = await steam.get_auth_session_ticket_response
	if issued[1] != _TICKET_ISSUED:
		steam.cancelAuthTicket(handle)
		return null

	_tickets[handle] = session
	session.exited.connect(
		_on_exited.bind(session),
		CONNECT_REFERENCE_COUNTED,
	)
	var ticket: PackedByteArray = made.buffer
	return {
		"steam_id": steam.getSteamID(),
		"ticket": ticket.slice(0, made.size),
	}


func _verify(request: NetwJoinRequest, proof: Variant) -> Variant:
	if _SteamStart.start(ProjectSettings.get_setting(_APP_ID_SETTING, 0)) \
			.status != 0:
		if not _warned:
			push_warning(
				"SteamTicket cannot check Steam accounts because Steam is not "
				+ "running on this server. Run the server signed in to Steam.",
			)
			_warned = true
		return OK

	match proof:
		{ "steam_id": var steam_id, "ticket": var ticket } when (
				steam_id is int and ticket is PackedByteArray
		):
			var steam := Engine.get_singleton(&"Steam")
			var session := request.session
			steam.requestUserInformation(steam_id, true)
			steam.endAuthSession(steam_id)
			if steam.beginAuthSession(ticket, ticket.size(), steam_id) != 0:
				return OK

			_begun[[session, request.peer_id]] = steam_id
			session.exited.connect(
				_on_exited.bind(session),
				CONNECT_REFERENCE_COUNTED,
			)
			session.player_left.connect(
				_on_player_left.bind(session),
				CONNECT_REFERENCE_COUNTED,
			)
			session.root.multiplayer.peer_disconnected.connect(
				_on_peer_disconnected.bind(session),
				CONNECT_REFERENCE_COUNTED,
			)
			session.root.multiplayer.connect(
				&"peer_authentication_failed",
				_on_peer_disconnected.bind(session),
				CONNECT_REFERENCE_COUNTED,
			)

			var checked: Array = await steam.validate_auth_ticket_response
			while checked[0] != steam_id:
				checked = await steam.validate_auth_ticket_response
			if checked[1] != _TICKET_VALID:
				_on_peer_disconnected(request.peer_id, session)
				return OK

			request.add_account(
				"steam:%d" % steam_id,
				steam.getFriendPersonaName(steam_id),
				false,
			)
	return OK


func _on_exited(_reason: int, _detail: String, session: NetwSession) -> void:
	var steam := Engine.get_singleton(&"Steam")
	for handle: int in _tickets.keys():
		if _tickets[handle] == session:
			steam.cancelAuthTicket(handle)
			_tickets.erase(handle)
	for key: Array in _begun.keys():
		if key[0] == session:
			_on_peer_disconnected(key[1], session)


func _on_player_left(
		player: NetwPlayer,
		_reason: NetwPlayer.LeaveReason,
		session: NetwSession,
) -> void:
	_on_peer_disconnected(player.peer_id, session)


func _on_peer_disconnected(peer_id: int, session: NetwSession) -> void:
	var key := [session, peer_id]
	if _begun.has(key):
		Engine.get_singleton(&"Steam").endAuthSession(_begun[key])
	_begun.erase(key)
