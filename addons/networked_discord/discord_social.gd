## Shows the player's [member party] on their Discord profile and connects
## the player with their Discord friends.
##
## A signed in player is always in a [DiscordParty], so a Join click on their
## profile joins that party, and the leader's server reaches every member at
## [signal NetwConnection.invited]. Only one [DiscordSocial] per process talks
## to Discord, and any other one has a [code]null[/code] [member client].
## [codeblock]
## @onready var session: NetwSession = Netw.session(self)
## @onready var discord: DiscordSocial = session.service(DiscordSocial)
##
## func _ready() -> void:
##     discord.details = "Bomber"
##     discord.friends_changed.connect(_on_discord_friends_changed)
##     session.connection.invited.connect(_on_invited)
##
## func _on_sign_in_pressed() -> void:
##     await discord.sign_in().wait()
##
## func _on_discord_friends_changed() -> void:
##     friend_list.clear()
##     for friend in discord.friends:
##         friend_list.add_item(discord.profile(friend).display_name)
##
## func _on_friend_list_item_activated(index: int) -> void:
##     discord.party.invite(discord.friends[index])
##
## func _on_invited(endpoint: NetwEndpoint) -> void:
##     await session.connection.join(endpoint).settled
## [/codeblock]
## A game that keeps a [ConnectBrowser] lets it answer
## [signal NetwConnection.invited], or turns off
## [member ConnectBrowser.join_invites] before joining from its own handler,
## so the player joins once.
## [br][br]
## Over ENet the host advertises its LAN address, so a host reached over the
## internet sets [member NetwConnection.join_address] after hosting.
## [br][br]
## [code]res://addons/networked_discord/ui/discord_panel.tscn[/code] is a
## ready made panel of friends, invitations and the party.
## [br][br]
## A Discord user ID that another peer reports is only a claim, so never key
## saved data, bans or inventory to it. A game without its own backend can
## check who is joining with [method prove].
class_name DiscordSocial
extends NetwService

## Emitted when [method sign_in] signs in the player [param user_id].
signal signed_in(user_id: int)
## Emitted when the player signs out or loses the Discord connection.
signal signed_out
## Emitted when the player signs in or out, when [member friends] gains or
## loses a friend, and when a friend starts or stops playing this game or goes
## online or offline. A change to one friend also emits
## [signal DiscordProfile.changed] on [method profile].
signal friends_changed

const _CREDENTIALS_PATH := "user://discord_%d.cfg"
const _DISCORD_TOKEN_URL := "https://discord.com/api/oauth2/token"
const _GRANT_FIELDS := {
	"authorization_code": ["code", "code_verifier", "redirect_uri"],
	"refresh_token": ["refresh_token"],
}
const _PROOF_RENEWAL_SECONDS := 300
const _CONNECTED_USER_WAIT_SECONDS := 2.0
const _AUTHORIZE_WARN_SECONDS := 60.0
const _AVATAR_DOWNLOADS := 4

## The Discord application ID with the Social SDK enabled.
@export var application_id: int
## The Steam app ID Discord launches for a Join click. [code]0[/code]
## launches this executable.
@export var steam_app_id: int
## Keeps the player signed in and [method prove] silent across restarts. The
## player is signed back in when the game starts, unless the game sets
## [member token_url] or runs headless. Then [method sign_in] does it. Someone
## who copies the player's files could use a saved proof for up to an hour.
@export var remember_sign_in := true
## Where [method sign_in] and [method prove] post their token requests. A
## game with its own server points it there, and the server adds the client
## secret and answers with Discord's response unchanged, as
## [url=https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-with-discord#step-4-exchanging-the-authorization-code-for-an-access-token]Discord's guide[/url]
## shows. A server that answers without a [code]refresh_token[/code] keeps
## the player signed in only until the game closes. Set it in the Inspector or
## in [method Node._ready]. It must use https, or http for a server on
## [code]127.0.0.1[/code] or [code]localhost[/code].
## [codeblock]
## func _ready() -> void:
##     discord.token_url = "https://api.mygame.com/discord/token"
##
## func _on_game_login(session_token: String) -> void:
##     discord.token_headers = ["Authorization: Bearer " + session_token]
##     await discord.sign_in().wait()
## [/codeblock]
@export var token_url := _DISCORD_TOKEN_URL:
	set(value):
		var host := value.trim_prefix("http://").get_slice("/", 0) \
				.get_slice(":", 0)
		if not value.begins_with("https://") and not (
				value.begins_with("http://")
				and host in ["127.0.0.1", "localhost"]
		):
			push_error(
				"DiscordSocial.token_url must use https. Use an https "
				+ "address, or http://127.0.0.1 for a server on this machine.",
			)
			return
		for header: String in token_headers:
			if value == _DISCORD_TOKEN_URL \
					and header.to_lower().begins_with("authorization:"):
				push_error(
					"DiscordSocial.token_url would send the Authorization "
					+ "header in token_headers to Discord. Clear "
					+ "DiscordSocial.token_headers first.",
				)
				return
		token_url = value
## Headers sent with each request to [member token_url], such as the
## player's session on the game's own server. An Authorization header needs
## [member token_url] set to the game's own server first.
var token_headers: PackedStringArray:
	set(value):
		for header: String in value:
			if token_url == _DISCORD_TOKEN_URL \
					and header.to_lower().begins_with("authorization:"):
				push_error(
					"DiscordSocial.token_headers would send an Authorization "
					+ "header to Discord. Set DiscordSocial.token_url to your "
					+ "own server first.",
				)
				return
		token_headers = value

@export_group("Presence")
## The first line under the game's name on the player's Discord profile.
@export var details: String
## The second line under the game's name on the player's Discord profile.
@export var state_text: String
## The large image's asset key on the Discord application.
@export var large_image: String
## The text shown over [member large_image].
@export var large_text: String
## The small image's asset key on the Discord application.
@export var small_image: String
## The text shown over [member small_image].
@export var small_text: String
## Lets friends join the [member party] without asking, subject to their
## Discord settings.
@export var public_party := false

## The signed in player's Discord user ID, or [code]0[/code] while signed out.
var user_id: int
## The Discord user IDs of the player's friends, empty while signed out.
## Friends playing this game come first, then friends online elsewhere, then
## offline friends. [signal friends_changed] tells when it changes.
var friends: Array[int]:
	get:
		var ids: Array[int] = []
		if user_id == 0:
			return ids
		for group: int in [
			DiscordRelationshipGroupType.ONLINE_PLAYING_GAME,
			DiscordRelationshipGroupType.ONLINE_ELSEWHERE,
			DiscordRelationshipGroupType.OFFLINE,
		]:
			for relation: DiscordRelationshipHandle in \
					_client.get_relationships_by_group(group):
				if DiscordRelationshipType.FRIEND in [
					relation.discord_relationship_type(),
					relation.game_relationship_type(),
				]:
					ids.append(relation.id())
		return ids
## The player's [DiscordParty] of Discord friends and its voice call. It is
## read-only.
var party := DiscordParty.new(self)

## The [DiscordClient] this node owns. It is [code]null[/code] after this
## node reported a configuration error, and on every [DiscordSocial] but the
## first in the process. Use it for what this node does not wrap, such as
## messages, voice settings and devices. It is read-only.
## [br][br]
## A second call to a [code]set_*_callback[/code] method replaces the first,
## so leave these to this node and its [member party].[br]
## - [method DiscordClient.set_status_changed_callback][br]
## - [method DiscordClient.set_activity_join_callback][br]
## - [method DiscordClient.set_activity_invite_created_callback][br]
## - [method DiscordClient.set_activity_invite_updated_callback][br]
## - [method DiscordClient.set_user_updated_callback][br]
## - [method DiscordClient.set_relationship_groups_updated_callback][br]
## - [method DiscordClient.set_lobby_created_callback][br]
## - [method DiscordClient.set_lobby_updated_callback][br]
## - [method DiscordClient.set_lobby_member_added_callback][br]
## - [method DiscordClient.set_lobby_member_removed_callback][br]
## - [method DiscordClient.set_lobby_member_updated_callback][br]
## - [method DiscordClient.set_voice_participant_changed_callback][br]
## - the callbacks of the [DiscordCall] on the party's lobby
var client: DiscordClient:
	get:
		return _client if _owner == self else null

static var _client: DiscordClient
static var _owner: DiscordSocial

var _signing_in := NetwPromise.resolved(0)
var _authorizing := NetwPromise.resolved("")
var _restoring: NetwPromise
var _proof_key: CryptoKey
var _proves: bool
var _verifier: DiscordAuthorizationCodeVerifier
var _profiles: Dictionary[int, DiscordProfile]
var _avatar_urls: Dictionary[int, String]
var _avatar_queue: Array[int]
var _downloads: int
var _proof: Dictionary


static func _proof_of(key: CryptoKey, token: String) -> Dictionary:
	var part := token.get_slice(".", 1).replace("-", "+").replace("_", "/")
	part += "=".repeat(posmod(-part.length(), 4))
	var claims: Variant = JSON.parse_string(
		Marshalls.base64_to_raw(part).get_string_from_utf8(),
	)
	var public_key := key.save_to_string(true)
	match claims:
		{ "exp": var expiry, "sub": var subject, "nonce": var nonce, .. } when (
				expiry is float and subject is String and nonce is String
				and nonce == public_key.sha256_text()
		):
			return {
				"key": key,
				"public_key": public_key,
				"token": token,
				"expires_at": expiry,
				"subject": subject,
			}
	return { }


static func _on_user_updated(id: int) -> void:
	var shown: DiscordProfile = _owner._profiles.get(id)
	if shown != null:
		_owner._avatar_queue.append(id)
		shown.changed.emit()


static func _on_relationship_groups_updated(_id: int) -> void:
	_owner.friends_changed.emit()


static func _on_token_revoked(result: DiscordClientResult) -> void:
	if not result.successful():
		push_warning(
			"Discord did not revoke the sign in (%s). " % result.error()
			+ "The player can remove the game under Authorized Apps in "
			+ "Discord.",
		)


static func _on_status_changed(status: int, error: int, _detail: int) -> void:
	var connected := status == DiscordClientStatus.READY
	if connected and not _owner._signing_in.is_settled:
		var user: DiscordUserHandle = _client.get_current_user_v2()
		_owner.user_id = user.id()
		if _owner._proof.get("subject", str(user.id())) != str(user.id()):
			_owner._proof = { }
			_owner._remember({ "proof_key": null, "id_token": null })
		_owner.signed_in.emit(_owner.user_id)
		_owner._avatar_queue.append_array(_owner._profiles.keys())
		_owner.friends_changed.emit()
		_owner._signing_in.resolve(_owner.user_id)
		return

	if (
			status != DiscordClientStatus.DISCONNECTED
			or error == DiscordClientError.NONE
	):
		return

	_owner._signing_in.reject(
		ERR_CANT_CONNECT,
		"Discord closed the connection. Call DiscordSocial.sign_in again.",
	)
	if _owner.user_id == 0:
		return

	_owner.user_id = 0
	_owner.signed_out.emit()
	_owner.friends_changed.emit()


static func _on_connected_user(
		_result: DiscordClientResult,
		user: DiscordUserHandle,
		promise_id: int,
) -> void:
	var authorizing := instance_from_id(promise_id) as NetwPromise
	if authorizing == null or authorizing.is_settled \
			or _owner._verifier != null:
		return
	assert(_owner._authorizing == authorizing)

	if user != null and user.username() == "arrpc":
		var reason := "Sign in cannot finish while Vesktop or another app " \
				+ "using arRPC is running. Close it or open the Discord app, " \
				+ "then sign in again."
		push_warning(reason)
		authorizing.reject(ERR_UNAVAILABLE, reason)
		return

	_owner._proof_key = Crypto.new().generate_rsa(2048)
	_owner._verifier = _client.create_authorization_code_verifier()
	var args := DiscordAuthorizationArgs.new()
	args.set_client_id(_owner.application_id)
	args.set_scopes(DiscordClient.get_default_communication_scopes())
	args.set_code_challenge(_owner._verifier.challenge())
	args.set_nonce(_owner._proof_key.save_to_string(true).sha256_text())
	_client.authorize(args, DiscordSocial._on_authorized.bind(promise_id))
	_owner.get_tree().create_timer(_AUTHORIZE_WARN_SECONDS).timeout.connect(
		DiscordSocial._on_authorize_timeout.bind(promise_id),
	)


static func _on_authorized(
		result: DiscordClientResult,
		code: String,
		redirect_uri: String,
		promise_id: int,
) -> void:
	var authorizing := instance_from_id(promise_id) as NetwPromise
	if authorizing == null or authorizing.is_settled:
		return
	if not result.successful():
		authorizing.reject(
			ERR_UNAUTHORIZED,
			"Discord did not authorize the sign in (%s). " % result.error()
			+ "Try again and approve the Discord page.",
		)
		return

	_owner._request_token(
		{
			"grant_type": "authorization_code",
			"code": code,
			"redirect_uri": redirect_uri,
			"code_verifier": _owner._verifier.verifier(),
		},
	).then(_owner._on_code_exchanged.bind(promise_id))


static func _on_authorize_timeout(promise_id: int) -> void:
	var authorizing := instance_from_id(promise_id) as NetwPromise
	if authorizing != null and not authorizing.is_settled:
		push_warning(
			"Discord has not answered the sign in for a minute. Approve the "
			+ "Discord page the browser opened, or close any Discord client "
			+ "mod and sign in again.",
		)


static func _on_token_updated(
		result: DiscordClientResult,
		promise_id: int,
) -> void:
	var signing_in := instance_from_id(promise_id) as NetwPromise
	if signing_in == null or signing_in.is_settled:
		return
	assert(
		_owner != null,
		"An unsettled sign in means its node still owns the client.",
	)
	if not result.successful():
		signing_in.reject(
			ERR_UNAUTHORIZED,
			"Discord refused the sign in (%s). " % result.error()
			+ "Call DiscordSocial.sign_in again.",
		)
		return
	_client.connect_discord()


func _process(_delta: float) -> void:
	if _owner != self:
		return
	Discord.run_callbacks()
	while _downloads < _AVATAR_DOWNLOADS and not _avatar_queue.is_empty():
		var id: int = _avatar_queue.pop_front()
		var user := _client.get_user(id)
		var url: String = "" if user == null else user.avatar_url(
			DiscordUserHandleAvatarType.PNG,
			DiscordUserHandleAvatarType.PNG,
		)
		if url.is_empty() or url == _avatar_urls.get(id, "") \
				or DisplayServer.get_name() == "headless":
			continue
		_avatar_urls[id] = url
		var request := HTTPRequest.new()
		add_child(request)
		request.request_completed.connect(
			_on_avatar_request_completed.bind(id, url, request),
		)
		_downloads += 1
		if request.request(url) != OK:
			request.request_completed.emit(
				HTTPRequest.RESULT_CANT_CONNECT,
				0,
				PackedStringArray(),
				PackedByteArray(),
			)


func _service_entered(_session: NetwSession) -> void:
	if application_id <= 0:
		push_error(
			"DiscordSocial has no application id. "
			+ "Set DiscordSocial.application_id.",
		)
		return
	if NetwService.is_transport_restricted():
		push_error(
			"DiscordSocial cannot run inside a Discord activity. "
			+ "Use DiscordActivityService there.",
		)
		return
	if _owner != null:
		return

	assert(_client == null, "The node that left last released the client.")
	_owner = self
	_client = DiscordClient.new()
	_client.set_user_updated_callback(DiscordSocial._on_user_updated)
	_client.set_relationship_groups_updated_callback(
		DiscordSocial._on_relationship_groups_updated,
	)
	_client.set_status_changed_callback(DiscordSocial._on_status_changed)
	_client.set_application_id(application_id)

	# A headless game is a server or a test run. A Join click cannot open it,
	# and it would replace the launch command and spend the saved sign in of
	# the player's game.
	if DisplayServer.get_name() == "headless":
		return
	get_tree().process_frame.connect(_on_process_frame, CONNECT_ONE_SHOT)
	var command := '"%s"' % OS.get_executable_path()
	if OS.has_feature("editor"):
		command += ' --path "%s"' % ProjectSettings.globalize_path("res://")
	var registered := false
	if steam_app_id > 0:
		registered = _client.register_launch_steam_application(
			application_id,
			steam_app_id,
		)
	else:
		registered = _client.register_launch_command(application_id, command)
	if not registered:
		push_warning(
			"Discord could not register how to launch this game. "
			+ "Check DiscordSocial.application_id.",
		)


func _service_exiting(_session: NetwSession) -> void:
	if _owner != self:
		return

	if get_tree().process_frame.is_connected(_on_process_frame):
		get_tree().process_frame.disconnect(_on_process_frame)
	_signing_in.reject(ERR_SKIP, "DiscordSocial left the scene tree.")
	_authorizing.reject(ERR_SKIP, "DiscordSocial left the scene tree.")
	_client.abort_authorize()
	_client.disconnect_discord()
	Discord.run_callbacks()
	_client = null
	user_id = 0
	_avatar_queue.clear()
	friends_changed.emit()
	_owner = null


func _remember(values: Dictionary) -> void:
	assert(
		values.get("refresh_token") == null
		or not values.refresh_token.is_empty(),
		"A refresh token is saved only from a response that held one.",
	)
	if not remember_sign_in:
		return
	var path := _CREDENTIALS_PATH % application_id
	assert(path.begins_with("user://"))
	var credentials := ConfigFile.new()
	credentials.load(path)
	for key: String in values:
		credentials.set_value("discord", key, values[key])
	if not FileAccess.file_exists(path):
		FileAccess.open(path, FileAccess.WRITE)
	FileAccess.set_unix_permissions(path, 384)
	credentials.save(path)


func _authorize() -> NetwPromise:
	if not _authorizing.is_settled:
		return _authorizing

	_authorizing = NetwPromise.new()
	_verifier = null
	var promise_id := _authorizing.get_instance_id()
	_client.get_discord_client_connected_user(
		application_id,
		DiscordSocial._on_connected_user.bind(promise_id),
	)
	get_tree().create_timer(_CONNECTED_USER_WAIT_SECONDS).timeout.connect(
		DiscordSocial._on_connected_user.bind(null, null, promise_id),
	)
	return _authorizing


func _renew_proof() -> NetwPromise:
	var left: float = _proof.get("expires_at", 0.0) \
			- Time.get_unix_time_from_system()
	if not _proves or left > _PROOF_RENEWAL_SECONDS:
		return NetwPromise.resolved("")
	return _authorize()


func _request_token(form: Dictionary) -> NetwPromise:
	var needed: Array = _GRANT_FIELDS.get(form.grant_type, [null])
	assert(form.has_all(needed))
	var fields: PackedStringArray = ["client_id=%d" % application_id]
	for field: String in form:
		fields.append(field + "=" + str(form[field]).uri_encode())
	var headers: PackedStringArray = [
		"Content-Type: application/x-www-form-urlencoded",
	]
	headers.append_array(token_headers)

	var requested := NetwPromise.new()
	var request := HTTPRequest.new()
	add_child(request)
	request.request_completed.connect(
		_on_token_request_completed.bind(request, requested),
	)
	var sent := request.request(
		token_url,
		headers,
		HTTPClient.METHOD_POST,
		"&".join(fields),
	)
	if sent != OK:
		request.request_completed.emit(
			HTTPRequest.RESULT_CANT_CONNECT,
			0,
			PackedStringArray(),
			PackedByteArray(),
		)
	return requested


## Signs the player in to Discord and resolves with [member user_id]. The
## first sign in opens Discord's authorization page in the browser. Later
## launches sign in without it while [member remember_sign_in] is on. A call
## while one is pending returns the same [NetwPromise], and a [method prove]
## that needs the page shares it. It fails with
## [constant @GlobalScope.ERR_UNAVAILABLE] while [member client] is
## [code]null[/code], and while Vesktop or another app using arRPC stands in
## for Discord.
## [br][br]
## A call while signed in renews a proof of [method prove] that is about to
## expire, and resolves once Discord answers.
## [br][br]
## The default [member token_url] needs Public Client and the redirect
## [code]http://127.0.0.1/callback[/code] turned on for
## [member application_id] in the Discord developer portal. A game with its
## own server sets [member token_url] and needs only the redirect. A game
## that only shows presence and the Join button needs neither, as long as
## the Discord desktop app is running.
func sign_in() -> NetwPromise:
	if _owner != self:
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"This DiscordSocial has no Discord client. Fix the error it "
			+ "reported, or sign in with the first DiscordSocial.",
		)
	if user_id != 0:
		var renewed := NetwPromise.new()
		_renew_proof().when_settled(renewed.resolve.bind(user_id))
		return renewed
	if not _signing_in.is_settled:
		_restoring = null
		return _signing_in

	_signing_in = NetwPromise.new()
	var promise_id := _signing_in.get_instance_id()
	var credentials := ConfigFile.new()
	if remember_sign_in:
		credentials.load(_CREDENTIALS_PATH % application_id)
	_proves = _proves or credentials.get_value("discord", "proves", false)
	var saved_key: String = credentials.get_value("discord", "proof_key", "")
	if not saved_key.is_empty():
		var key := CryptoKey.new()
		_proof = { }
		if key.load_from_string(saved_key) == OK:
			_proof = _proof_of(
				key,
				credentials.get_value("discord", "id_token", ""),
			)
		if _proof.is_empty():
			_remember({ "proof_key": null, "id_token": null })

	var saved: String = credentials.get_value("discord", "refresh_token", "")
	if saved.is_empty():
		_authorize() \
				.then(_on_access_granted.bind(promise_id)) \
				.catch_error(_on_access_refused.bind(promise_id))
	else:
		_request_token(
			{ "grant_type": "refresh_token", "refresh_token": saved },
		).then(_on_token_refreshed.bind(promise_id))
	return _signing_in


## Signs the player out, forgets the proof [method prove] keeps, deletes the
## saved sign in and asks Discord to revoke it, so the next [method sign_in]
## opens the authorization page again. A pending [method sign_in] fails with
## [constant @GlobalScope.ERR_SKIP]. With [member token_url] set to the
## game's own server, that server revokes the sign in.
func sign_out() -> void:
	if _owner != self:
		return

	_proof = { }
	var path := _CREDENTIALS_PATH % application_id
	assert(path.begins_with("user://"))
	var credentials := ConfigFile.new()
	credentials.load(path)
	var saved: String = credentials.get_value("discord", "refresh_token", "")
	DirAccess.remove_absolute(path)
	if not saved.is_empty() and token_url == _DISCORD_TOKEN_URL:
		_client.revoke_token(
			application_id,
			saved,
			DiscordSocial._on_token_revoked,
		)

	_client.abort_authorize()
	_client.disconnect_discord()
	_signing_in.reject(ERR_SKIP, "The player signed out.")
	_authorizing.reject(ERR_SKIP, "The player signed out.")

	var was_signed_in := user_id != 0
	user_id = 0
	_avatar_queue.clear()
	if was_signed_in:
		signed_out.emit()
		friends_changed.emit()


## Returns the [DiscordProfile] of the Discord user ID [param id], the same
## object for the same ID. Its properties are empty while the player is
## signed out or Discord does not know [param id], and fill in when they
## can. Its [member DiscordProfile.avatar] starts downloading here.
func profile(id: int) -> DiscordProfile:
	if not _profiles.has(id):
		_profiles[id] = DiscordProfile.new(self, id)
		if user_id != 0:
			_avatar_queue.append(id)
	return _profiles[id]


## Returns a proof of the player's Discord account that answers
## [param challenge], or empty bytes when the player declines or Discord
## refuses. The server checks it with [method DiscordIdToken.admit].
## [br][br]
## A proof lasts an hour. It opens a Discord page only when it holds no proof
## with more than five minutes left. [method sign_in] makes a proof when it
## opens the page, and [member remember_sign_in] keeps it across restarts.
## Once a game has called it, [method DiscordParty.join] and a Join click
## renew a proof about to expire before the player follows the leader.
## Raise [member NetwMultiplayer.auth_timeout] on the server and the client to
## give the player time to approve the page. A pending [method sign_in] that
## shows the page shares it.
## [br][br]
## Discord recommends linking the Discord account to the game's own account
## and not using it as the only login, because a player whose Discord
## account is banned would lose their data. See
## [url=https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-with-discord#recommended-integration-path]Discord's guide[/url].
## [codeblock]
## func _init() -> void:
##     Netw.configure_admission(admit).proof(prove)
##
## func prove(challenge: String) -> PackedByteArray:
##     return await discord.prove(challenge)
##
## func admit(request: NetwJoinRequest) -> Error:
##     return await DiscordIdToken.admit(request, discord.application_id)
## [/codeblock]
func prove(challenge: String) -> PackedByteArray:
	if _owner != self:
		push_warning(
			"This DiscordSocial has no Discord client. Fix the error it "
			+ "reported, or call prove on the first DiscordSocial.",
		)
		return PackedByteArray()

	if not _proves:
		_proves = true
		_remember({ "proves": true })
	var renewing := _renew_proof()
	if not renewing.is_settled:
		await renewing.wait()
	if renewing.is_failed:
		if renewing.code not in [ERR_SKIP, ERR_UNAVAILABLE]:
			push_warning(renewing.detail)
		return PackedByteArray()

	assert(not _proof.is_empty(), "An authorization keeps its proof.")
	var signature := Crypto.new().sign(
		HashingContext.HASH_SHA256,
		challenge.sha256_buffer(),
		_proof.key,
	)
	return var_to_bytes([_proof.token, _proof.public_key, signature])


func _on_token_request_completed(
		result: int,
		code: int,
		_headers: PackedStringArray,
		body: PackedByteArray,
		request: HTTPRequest,
		requested: NetwPromise,
) -> void:
	request.queue_free()
	if result != HTTPRequest.RESULT_SUCCESS:
		requested.resolve(
			{
				"error": "DiscordSocial could not reach %s. " % token_url
				+ "Check the network connection and DiscordSocial.token_url.",
			},
		)
		return

	var grant: Variant = JSON.parse_string(body.get_string_from_utf8())
	match [code, grant]:
		[200, { "access_token": var access, .. }] when access is String:
			requested.resolve(grant)
		[400, { "error": "invalid_grant", .. }]:
			requested.resolve(
				{
					"error": "Discord no longer accepts the saved sign in. "
					+ "Call DiscordSocial.sign_in again.",
				},
			)
		[var status, _] when (
				token_url == _DISCORD_TOKEN_URL and status in [400, 401]
		):
			requested.resolve(
				{
					"error": "Discord refused the sign in. Turn on Public "
					+ "Client for the application, or set "
					+ "DiscordSocial.token_url to your own server.",
				},
			)
		_:
			requested.resolve(
				{
					"error": "%s answered HTTP %d to a sign in. "
					% [token_url, code]
					+ "Check the server at DiscordSocial.token_url.",
				},
			)


func _on_token_refreshed(grant: Dictionary, promise_id: int) -> void:
	var signing_in := instance_from_id(promise_id) as NetwPromise
	if signing_in == null or signing_in.is_settled:
		return
	if grant.has("error"):
		DirAccess.remove_absolute(_CREDENTIALS_PATH % application_id)
		signing_in.reject(ERR_UNAUTHORIZED, grant.error)
		return

	var held := str(grant.get("scope", "")).split(" ", false)
	var covered := true
	var wanted := DiscordClient.get_default_communication_scopes()
	for scope: String in wanted.split(" ", false):
		covered = covered and scope in held
	var refresh: Variant = grant.get("refresh_token")
	if refresh is not String or refresh.is_empty() or not covered:
		refresh = null
	_remember({ "refresh_token": refresh })

	if covered:
		_client.update_token(
			DiscordAuthorizationTokenType.BEARER,
			grant.access_token,
			DiscordSocial._on_token_updated.bind(promise_id),
		)
	elif signing_in == _restoring:
		signing_in.reject(
			ERR_UNAUTHORIZED,
			"The saved Discord sign in needs the Discord page again. "
			+ "Call DiscordSocial.sign_in.",
		)
	else:
		_authorize() \
				.then(_on_access_granted.bind(promise_id)) \
				.catch_error(_on_access_refused.bind(promise_id))


func _on_access_granted(access: String, promise_id: int) -> void:
	var signing_in := instance_from_id(promise_id) as NetwPromise
	if signing_in != null and not signing_in.is_settled:
		_client.update_token(
			DiscordAuthorizationTokenType.BEARER,
			access,
			DiscordSocial._on_token_updated.bind(promise_id),
		)


func _on_access_refused(code: Error, detail: String, promise_id: int) -> void:
	var signing_in := instance_from_id(promise_id) as NetwPromise
	if signing_in != null:
		signing_in.reject(code, detail)


func _on_code_exchanged(grant: Dictionary, promise_id: int) -> void:
	var authorizing := instance_from_id(promise_id) as NetwPromise
	if authorizing == null or authorizing.is_settled:
		return
	if grant.has("error"):
		authorizing.reject(ERR_UNAUTHORIZED, grant.error)
		return

	_proof = { }
	match grant:
		{ "id_token": var token, .. } when token is String:
			_proof = _proof_of(_proof_key, token)
	if _proof.is_empty():
		authorizing.reject(
			ERR_UNAUTHORIZED,
			"The answer from %s has no valid ID token. " % token_url
			+ "Return Discord's answer unchanged.",
		)
		return

	var refresh: Variant = grant.get("refresh_token")
	if refresh is not String or refresh.is_empty():
		refresh = null
	_remember(
		{
			"refresh_token": refresh,
			"proof_key": _proof_key.save_to_string(),
			"id_token": _proof.token,
		},
	)
	authorizing.resolve(grant.access_token)


func _on_process_frame() -> void:
	var credentials := ConfigFile.new()
	credentials.load(_CREDENTIALS_PATH % application_id)
	if not remember_sign_in or token_url != _DISCORD_TOKEN_URL \
			or not _signing_in.is_settled \
			or not credentials.has_section_key("discord", "refresh_token"):
		return
	_restoring = sign_in()
	await _restoring.wait()


func _on_avatar_request_completed(
		_result: int,
		code: int,
		_headers: PackedStringArray,
		body: PackedByteArray,
		id: int,
		url: String,
		request: HTTPRequest,
) -> void:
	_downloads -= 1
	request.queue_free()
	var image := Image.new()
	if code != 200 or url != _avatar_urls.get(id, "") \
			or image.load_png_from_buffer(body) != OK:
		return
	_profiles[id]._avatar = ImageTexture.create_from_image(image)
	_profiles[id].changed.emit()
