## The player's Nakama account, shared by every Nakama node in the session.
##
## [codeblock]
## @onready var nakama: NakamaAccount = session.service(NakamaAccount)
##
## func _on_play_pressed() -> void:
##     await nakama.sign_in_steam().wait()
##
## func _on_link_steam_pressed() -> void:
##     await nakama.link_steam().wait()
##
## func _on_email_sign_in(email: String, password: String) -> void:
##     nakama.use_session(
##         await nakama.client.authenticate_email_async(email, password))
## [/codeblock]
class_name NakamaAccount
extends NetwService

## Emitted when the player is signed in and [member socket] is connected.
signal signed_in

## Emitted when the player loses their session.
signal signed_out

const _Nakama := preload("res://addons/com.heroiclabs.nakama/Nakama.gd")
const _STEAM_RESULT_OK := 1
const _GUEST_SLOT := "user://nakama_guest_%d"
const _GUEST_SLOTS := 64
const _GUEST_STALE_SECONDS := 15.0
const _JOIN_TOKEN_RPC := "networked_join_token"
const _SDK_MEMBERS := {
	"Nakama": ["create_client", "create_socket_from"],
	"NakamaClient": [
		"_api_client",
		"authenticate_device_async",
		"authenticate_steam_async",
		"link_steam_async",
		"session_refresh_async",
		"get_account_async",
		"get_users_async",
		"rpc_async",
		"list_matches_async",
		"write_storage_objects_async",
		"read_storage_objects_async",
		"list_storage_objects_async",
		"delete_storage_objects_async",
	],
	"NakamaAPI.ApiClient": ["_http_adapter"],
	"NakamaSocket": [
		"logger",
		"closed",
		"connected",
		"connection_error",
		"connect_async",
		"close",
		"is_connected_to_host",
		"create_match_async",
		"join_match_async",
		"join_matched_async",
		"leave_match_async",
		"send_match_state_async",
		"send_match_state_raw_async",
		"received_match_presence",
		"received_match_state",
		"received_matchmaker_matched",
		"create_party_async",
		"join_party_async",
		"leave_party_async",
		"accept_party_member_async",
		"remove_party_member_async",
		"send_party_data_async",
		"add_matchmaker_party_async",
		"received_party",
		"received_party_presence",
		"received_party_leader",
		"received_party_close",
		"received_party_data",
		"received_party_join_request",
		"received_party_matchmaker_ticket",
		"received_notification",
	],
}

static var _token_owner: NakamaAccount

## The Nakama server's host name or IP address, without a scheme such as
## [code]https://[/code].
@export var host := "127.0.0.1"

## The Nakama server's client port, its [code]socket.port[/code] setting.
## Nakama listens on [code]7350[/code] unless its configuration changes it.
@export var port := 7350

## The Nakama server's [code]socket.server_key[/code] setting, from its
## configuration file or the [code]--socket.server_key[/code] command line
## flag. Nakama uses [code]defaultkey[/code] when it is not set, so a public
## server sets its own key and the game sets the same one here.
@export var server_key := "defaultkey"

## Connects over https and wss, for a server reached through TLS.
@export var use_ssl := false

## The signed in Nakama user ID, empty while signed out.
var user_id: String

## The player's Nakama display name, or their username when they have none.
var display_name: String

## The [NakamaClient] for this server, for any Nakama call this node does
## not make. It is read-only.
var client: NakamaClient:
	get:
		if _client != null:
			return _client
		_client = _facade.create_client(
			server_key,
			host,
			port,
			"https" if use_ssl else "http",
			_Nakama.DEFAULT_TIMEOUT,
			NakamaLogger.LOG_LEVEL.WARNING,
		)
		if OS.has_feature("web"):
			_client._api_client._http_adapter.child_entered_tree.connect(
				_on_http_adapter_child_entered_tree,
			)
		return _client

## The signed in [NakamaSession], or [code]null[/code] while signed out. It
## is read-only.
var session: NakamaSession

## The realtime [NakamaSocket], or [code]null[/code] until it connects. It is
## read-only.
var socket: NakamaSocket

## The player's [NakamaParty] of Nakama friends. It is read-only.
var party := NakamaParty.new(self)

var _facade := _Nakama.new()
var _client: NakamaClient
var _opening: NakamaSocket
var _signing_in := NetwPromise.resolved("")
var _attempts := 0
var _steam_ticket_handle := 0
var _guest_slot := ""
var _warned_join_token := false
var _guest_heartbeat := Timer.new()


static func _static_init() -> void:
	var scripts := {
		"Nakama": _Nakama,
		"NakamaClient": NakamaClient,
		"NakamaAPI.ApiClient": NakamaAPI.ApiClient,
		"NakamaSocket": NakamaSocket,
	}
	var missing := PackedStringArray()
	for owner: String in _SDK_MEMBERS:
		var script: Script = scripts[owner]
		var names := { }
		for list: Array in [
			script.get_script_method_list(),
			script.get_script_property_list(),
			script.get_script_signal_list(),
		]:
			for entry: Dictionary in list:
				names[entry.name] = true
		for member: String in _SDK_MEMBERS[owner]:
			if not names.has(member):
				missing.append("%s.%s" % [owner, member])
	if not missing.is_empty():
		push_warning(
			"The Nakama SDK in addons/com.heroiclabs.nakama has no %s. "
			% ", ".join(missing)
			+ "Install the SDK that ships with networked_nakama.",
		)


func _init() -> void:
	add_child(_facade)
	_guest_heartbeat.wait_time = _GUEST_STALE_SECONDS / 3.0
	_guest_heartbeat.timeout.connect(_on_guest_heartbeat_timeout)
	add_child(_guest_heartbeat)


func _enter_tree() -> void:
	if _token_owner == null:
		_token_owner = self


func _exit_tree() -> void:
	_guest_heartbeat.stop()
	_mark_guest_slot(0)
	_guest_slot = ""
	if _token_owner == self:
		NetwIdToken.provide(NakamaIdToken.ISSUER, Callable())
		_token_owner = null


func _service_exiting(_session: NetwSession) -> void:
	_signing_in.reject(ERR_SKIP, "NakamaAccount left the scene tree.")
	_drop_session()


## Signs in a guest kept on this device, unless the player is already
## signed in, and resolves [member user_id] once [member socket] connects.
## Each instance of the game running at once gets its own guest.
func sign_in() -> NetwPromise:
	if socket != null:
		return NetwPromise.resolved(user_id)
	if not _signing_in.is_settled:
		return _signing_in
	if session != null:
		use_session(session)
		return _signing_in

	_signing_in = NetwPromise.new()
	_authenticate(false)
	return _signing_in


## Signs in with the player's Steam account, which the Nakama server checks
## with its Steam publisher key. Steam must be running, which a
## [SteamSocial] in the session arranges.
func sign_in_steam() -> NetwPromise:
	if _signing_in.is_settled:
		_signing_in = NetwPromise.new()
	_authenticate(true)
	return _signing_in


## Links the player's Steam account to the signed in Nakama account, so
## [method sign_in_steam] signs in to it on any device.
func link_steam() -> NetwPromise:
	var linking := NetwPromise.new()
	sign_in() \
			.then(_on_signed_in_to_link.bind(linking)) \
			.catch_error(linking.reject)
	return linking


## Signs in with [param made], a [NakamaSession] the game made with
## [member client].
func use_session(made: NakamaSession) -> void:
	if made == null or made.is_exception() or not made.is_valid():
		push_error(
			"NakamaAccount.use_session got no signed in session. Check the "
			+ "result of the Nakama sign in before passing it.",
		)
		return

	_attempts += 1
	if _signing_in.is_settled:
		_signing_in = NetwPromise.new()
	var lost := socket
	socket = null
	if lost != null:
		lost.close()
	if _token_owner == self:
		NetwIdToken.provide(NakamaIdToken.ISSUER, Callable())
	session = made
	user_id = made.user_id
	display_name = made.username
	_schedule_refresh(made)

	_opening = _facade.create_socket_from(client)
	_opening.connected.connect(_on_socket_connected.bind(_opening, made))
	_opening.connection_error.connect(
		_on_socket_connection_error.bind(_opening),
	)
	_opening.closed.connect(_on_socket_closed.bind(_opening))
	_opening.connect_async(made)


func _authenticate(with_steam: bool) -> void:
	_attempts += 1
	var attempt := _attempts
	var signing_in := _signing_in
	var made: NakamaSession
	if with_steam:
		var ticket := await _steam_ticket(signing_in)
		if ticket.is_empty():
			return
		made = await client.authenticate_steam_async(ticket, null, true)
		Engine.get_singleton(&"Steam").cancelAuthTicket(_steam_ticket_handle)
	else:
		var claimed := ConfigFile.new()
		var path := ""
		for slot in _GUEST_SLOTS:
			path = _GUEST_SLOT % slot
			claimed = ConfigFile.new()
			claimed.load(path)
			var holder: int = claimed.get_value("guest", "pid", 0)
			var alive: float = claimed.get_value("guest", "alive", 0.0)
			var age := Time.get_unix_time_from_system() - alive
			if holder in [0, OS.get_process_id()] or age > _GUEST_STALE_SECONDS:
				break
		var hex := Crypto.new().generate_random_bytes(16).hex_encode()
		var guest_id: String = claimed.get_value(
			"guest",
			"id",
			"%s-%s-%s-%s-%s" % [
				hex.substr(0, 8),
				hex.substr(8, 4),
				hex.substr(12, 4),
				hex.substr(16, 4),
				hex.substr(20, 12),
			],
		)
		claimed.set_value("guest", "id", guest_id)
		claimed.save(path)
		_guest_slot = path
		_mark_guest_slot(OS.get_process_id())
		_guest_heartbeat.start()
		made = await client.authenticate_device_async(guest_id, null, true)

	if attempt != _attempts:
		return
	if made.is_exception():
		signing_in.reject(
			ERR_UNAUTHORIZED,
			"Nakama refused the sign in (%s). " % made.get_exception().message
			+ "Check NakamaAccount.host, port and server_key.",
		)
		return
	use_session(made)


func _steam_ticket(asking: NetwPromise) -> String:
	if not Engine.has_singleton(&"Steam"):
		asking.reject(
			ERR_UNAVAILABLE,
			"GodotSteam is not installed. Install GodotSteam to sign in "
			+ "with Steam.",
		)
		return ""
	var steam := Engine.get_singleton(&"Steam")
	if steam.get_steam_init_result().get("status") != 0:
		asking.reject(
			ERR_UNAVAILABLE,
			"Steam is not running. Add a SteamSocial to the session before "
			+ "signing in with Steam.",
		)
		return ""

	_steam_ticket_handle = steam.getAuthTicketForWebApi("")
	var issued: Array = await steam.get_ticket_for_web_api
	while issued[0] != _steam_ticket_handle:
		issued = await steam.get_ticket_for_web_api
	if issued[1] != _STEAM_RESULT_OK:
		steam.cancelAuthTicket(_steam_ticket_handle)
		asking.reject(
			ERR_UNAUTHORIZED,
			"Steam issued no ticket (result %d). " % issued[1]
			+ "Check that the player is signed in to Steam.",
		)
		return ""
	return PackedByteArray(issued[3]).slice(0, issued[2]).hex_encode()


func _schedule_refresh(made: NakamaSession) -> void:
	if made.refresh_token.is_empty() or not is_inside_tree():
		return
	var lifetime := made.expire_time - Time.get_unix_time_from_system()
	get_tree().create_timer(maxf(lifetime * 0.8, 1.0)).timeout.connect(
		_on_refresh_timeout.bind(made),
	)


func _fetch_join_token(nonce: String) -> NetwPromise:
	var answering := NetwPromise.new()
	_ask_join_token(answering, nonce)
	return answering


func _ask_join_token(answering: NetwPromise, nonce: String) -> void:
	if session == null:
		answering.reject(ERR_UNAUTHORIZED, "NakamaAccount is signed out.")
		return
	var answer: NakamaAPI.ApiRpc = await client.rpc_async(
		session,
		_JOIN_TOKEN_RPC,
		JSON.stringify({ "nonce": nonce }),
	)
	if answer.is_exception():
		var reason := "Nakama refused a join token (%s). " \
				% answer.get_exception().message \
				+ "Check NETWORKED_JOIN_KEY in the server's runtime environment."
		if answer.get_exception().status_code == 404:
			reason = "The Nakama server has no %s RPC, so this player joins " \
					% _JOIN_TOKEN_RPC \
					+ "with no Nakama account. Install addons/networked_nakama/" \
					+ "server/networked.js on the server."
		if not _warned_join_token:
			push_warning(reason)
			_warned_join_token = true
		answering.reject(ERR_UNAVAILABLE, reason)
		return

	match JSON.parse_string(answer.payload):
		{ "token": var token, .. } when token is String:
			answering.resolve(token)
			return
	answering.reject(
		ERR_INVALID_DATA,
		"The Nakama server answered a join token request with no token. "
		+ "Update networked.js on the server.",
	)


func _mark_guest_slot(holder: int) -> void:
	if _guest_slot.is_empty():
		return
	var slot := ConfigFile.new()
	slot.load(_guest_slot)
	slot.set_value("guest", "pid", holder)
	slot.set_value("guest", "alive", Time.get_unix_time_from_system())
	slot.save(_guest_slot)


func _drop_session() -> void:
	var was_signed_in := session != null
	var lost := socket
	session = null
	socket = null
	_opening = null
	user_id = ""
	display_name = ""
	if lost != null:
		lost.close()
	if was_signed_in and _token_owner == self:
		NetwIdToken.provide(NakamaIdToken.ISSUER, Callable())
	if was_signed_in:
		signed_out.emit()


func _on_signed_in_to_link(_user_id: String, linking: NetwPromise) -> void:
	var ticket := await _steam_ticket(linking)
	if ticket.is_empty():
		return
	var linked: NakamaAsyncResult = await client.link_steam_async(
		session,
		ticket,
	)
	Engine.get_singleton(&"Steam").cancelAuthTicket(_steam_ticket_handle)
	if linked.is_exception():
		linking.reject(
			ERR_UNAUTHORIZED,
			"Nakama refused to link the Steam account (%s). "
			% linked.get_exception().message
			+ "Check the server's Steam publisher key, and that no other "
			+ "Nakama account holds this Steam account.",
		)
		return
	linking.resolve(user_id)


func _on_socket_connected(opening: NakamaSocket, made: NakamaSession) -> void:
	if opening != _opening:
		return
	var account: NakamaAPI.ApiAccount = await client.get_account_async(made)
	if opening != _opening:
		return
	if not account.is_exception() \
			and not account.user.display_name.is_empty():
		display_name = account.user.display_name
	socket = opening
	_opening = null
	if _token_owner == self:
		NetwIdToken.provide(NakamaIdToken.ISSUER, _fetch_join_token)
	_signing_in.resolve(user_id)
	signed_in.emit()


func _on_socket_connection_error(
		_error: Variant,
		opening: NakamaSocket,
) -> void:
	if opening != _opening:
		return
	_opening = null
	_signing_in.reject(
		ERR_CANT_CONNECT,
		"NakamaAccount could not connect to %s:%d. " % [host, port]
		+ "Check NakamaAccount.host and port, and that the server runs.",
	)


func _on_socket_closed(opening: NakamaSocket) -> void:
	if opening == socket:
		socket = null
		return
	if opening == _opening:
		_on_socket_connection_error(null, opening)


func _on_refresh_timeout(made: NakamaSession) -> void:
	if session != made:
		return
	var refreshed: NakamaSession = await client.session_refresh_async(made)
	if session != made:
		return
	if refreshed.is_exception():
		push_warning(
			"NakamaAccount could not refresh the Nakama session (%s). "
			% refreshed.get_exception().message
			+ "Call NakamaAccount.sign_in again.",
		)
		_drop_session()
		return
	session = refreshed
	_schedule_refresh(refreshed)


func _on_guest_heartbeat_timeout() -> void:
	_mark_guest_slot(OS.get_process_id())


# The browser already inflates gzip, so the SDK inflating it again fails
# (godot#116574).
func _on_http_adapter_child_entered_tree(node: Node) -> void:
	if node is HTTPRequest:
		node.accept_gzip = false
