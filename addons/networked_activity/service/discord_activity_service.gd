## Runs the Discord SDK handshake and connects the session to the Activity.
##
## [method in_discord] is the registration gate, so a normal build never loads
## this service. [member rendezvous] finds the server.
## [codeblock]
## var discord := Netw.session(self).service(DiscordActivityService)
## await discord.start()
## await discord.authenticate()
## await discord.connect_activity(&"Dev")
## [/codeblock]
class_name DiscordActivityService
extends NetwService

signal activity_ready()

signal identity_resolved(user: DiscordSDK.DiscordUser)

signal activity_failed(reason: String)

## Correlate entries to peers through [method device_id].
signal roster_changed(participants: Array)

signal layout_changed(layout_mode: int)

signal orientation_changed(screen_orientation: int)

signal state_changed(from: State, to: State)

enum State {
	IDLE,
	READY,
	AUTHENTICATED,
}

var state: State = State.IDLE

## Discord application id. The handshake and OAuth need it.
@export var client_id: String = ""

@export_group("OAuth")

## Where [method authenticate] exchanges its OAuth code. A bare path is mapped
## to [code]/.proxy/<path>[/code].
@export var token_endpoint: String = "token"

## Add scopes only when the Worker and the developer portal allow them.
@export var scopes: Array[String] = ["identify", "rpc.activities.write"]

@export_group("")

## [method connect_activity] fails with [constant ERR_UNCONFIGURED] while unset.
@export var rendezvous: DiscordRendezvous

@export var encourage_hw_accel: bool = true

## [code]null[/code] before [method authenticate].
var user: DiscordSDK.DiscordUser

@onready var session: NetwSession = Netw.session(self)

var _sdk: DiscordSDK
var _instance_id: String = ""
var _channel_id: String = ""
var _guild_id: String = ""
var _device_id: String = ""
var _detected: bool = false
var _started: bool = false

# Last roster reported by Discord, newest wins. Backs participants().
var _participants: Array = []


# Wires the core transport-restriction probe as soon as this script loads, which
# (because a scene loads all its scripts before instancing any node) happens
# before any directory checks _should_register. So WebRTC/native directories see
# the embed verdict and stay dormant in the iframe. A build that never references
# this class never loads it, so a normal game pays nothing.
static func _static_init() -> void:
	NetwService.set_transport_restricted_probe(_detect_embedded)


## [code]true[/code] when a Discord [code]instance_id[/code] exists.
func in_discord() -> bool:
	_detect()
	return not _instance_id.is_empty()


## The id every participant of the Activity shares.
func instance_id() -> String:
	_detect()
	return _instance_id


func channel_id() -> String:
	return _channel_id


func guild_id() -> String:
	return _guild_id


## The Discord user id, empty before [method authenticate].
func device_id() -> String:
	return _device_id


# Registration is bound to actually being embedded, so a normal build keeps its
# usual backends and never pays for the Discord path.
func _should_register() -> bool:
	return in_discord()


func _service_entered(entered: NetwSession) -> void:
	if rendezvous == null:
		push_warning(
			"DiscordActivityService has no rendezvous. Set "
			+ "DiscordActivityService.rendezvous to a DedicatedDiscordRendezvous "
			+ "or a NakamaDiscordRendezvous.",
		)
	else:
		rendezvous.bind(entered)
	_push_identity()
	# The SDK is parented in start(), not here: add_child during service setup
	# fails ("parent busy"), and the SDK's _ready() must run (it sets up the JS
	# bridge) before start() calls init(), or init() no-ops and ready() hangs.
	_sdk = _create_sdk()
	if _sdk != null:
		_sdk.name = &"DiscordSDK"


## Runs the SDK handshake. Without an SDK it is ready at once.
func start() -> bool:
	if _started:
		return true
	_detect()
	if _sdk == null:
		# No browser SDK to handshake. The connect path still runs because the
		# rendezvous only needs the instance id.
		_started = true
		_set_state(State.READY)
		activity_ready.emit()
		return true
	if client_id.is_empty():
		activity_failed.emit("Discord client_id unset")
		return false
	# Parent the SDK now (the service is settled in the tree, so add_child is
	# synchronous) so its _ready() runs and sets up the JS bridge before init().
	if not _sdk.is_inside_tree():
		add_child(_sdk)
	_sdk.init(client_id)
	await _sdk.ready()
	_instance_id = _sdk.instance_id
	_channel_id = _sdk.channel_id
	_guild_id = _sdk.guild_id
	if encourage_hw_accel:
		await _sdk.command_encourage_hardware_acceleration()
	_connect_sdk_signals()
	_started = true
	_set_state(State.READY)
	activity_ready.emit()
	return true


## Resolves [member user] through Discord OAuth.
## [codeblock lang=text]
## authorize(scopes) -> code
## token_endpoint    -> access_token
## authenticate      -> user
## [/codeblock]
func authenticate() -> bool:
	if _sdk == null:
		return false
	if not await start():
		return false
	var authorize := await _sdk.command_authorize("code", scopes, "")
	if authorize == null or authorize.code.is_empty():
		activity_failed.emit("Discord authorize returned no code")
		return false
	var access_token := await _exchange_code(authorize.code)
	if access_token.is_empty():
		activity_failed.emit("Token exchange failed")
		return false
	var auth := await _sdk.command_authenticate(access_token)
	if auth == null or auth.user == null:
		activity_failed.emit("Discord authenticate failed")
		return false
	user = auth.user
	# Stable backend identity: the same Discord user reconnects as the same user.
	if not user.id.is_empty():
		_device_id = user.id
		_push_identity()
	_set_state(State.AUTHENTICATED)
	identity_resolved.emit(user)
	# Subscribe and fetch the roster now that the session is authenticated.
	# Discord rejects both before authorize completes, so doing this in start()
	# produced a wall of SUBSCRIBE errors.
	_subscribe_activity_events()
	_refresh_roster()
	return true


## Joins the server [member rendezvous] finds, or hosts one when it finds none.
## Call it again to rejoin after [signal NetwSession.exited].
## [codeblock lang=text]
## find -> endpoint        join it, and host if that fails
## find -> null            host
## host                    claim -> winner, leave and join the winner
## [/codeblock]
func connect_activity(username: StringName, join_args: Array = []) -> Error:
	if not await start():
		return ERR_UNAVAILABLE
	if rendezvous == null:
		activity_failed.emit("DiscordActivityService has no rendezvous.")
		return ERR_UNCONFIGURED
	_push_identity()
	var connection := session.connection
	var prepare: Callable = Netw.prepare_join
	var as_player := [self, username] + join_args
	var endpoint := await rendezvous.find(_instance_id, connection)
	var transport := rendezvous.host_transport()
	if endpoint == null and transport.is_empty():
		activity_failed.emit("No server hosts this Activity.")
		return ERR_UNAVAILABLE
	prepare.callv(as_player)
	var attempt: NetwPromise
	if endpoint != null:
		attempt = connection.join(endpoint)
		await attempt.wait()
	if attempt == null or (attempt.is_failed and not transport.is_empty()):
		attempt = connection.host(transport, rendezvous.host_settings())
		await attempt.wait()
		var winner: NetwEndpoint = null
		if attempt.is_completed:
			winner = await rendezvous.claim(_instance_id, connection)
		if winner != null:
			await session.leave().wait()
			prepare.callv(as_player)
			attempt = connection.join(winner)
			await attempt.wait()
	if attempt.is_failed:
		activity_failed.emit(
			"Activity connect failed: %s" % error_string(attempt.code),
		)
	return attempt.code


## The latest roster, empty before the first [signal roster_changed].
func participants() -> Array:
	return _participants


## Does nothing before [method start] or without an SDK.
func set_presence(state: String, details: String) -> void:
	if _sdk == null or not _started:
		return
	await _sdk.command_set_activity(state, details)


## Does nothing without an SDK.
func open_invite_dialog() -> void:
	if _sdk == null:
		return
	await _sdk.command_open_invite_dialog()


## Does nothing without an SDK.
func share_moment(media_url: String) -> void:
	if _sdk == null:
		return
	await _sdk.command_open_share_moment_dialog(media_url)


## Does nothing without an SDK.
func open_external_link(url: String) -> void:
	if _sdk == null:
		return
	await _sdk.command_open_external_link(url)


# Override point. Creates the Discord SDK node, or null when there is no browser
# to host the postMessage handshake. A test fixture returns null to run the
# connect path with no real SDK.
func _create_sdk() -> DiscordSDK:
	if _is_browser():
		return DiscordSDK.new()
	return null


# Override point. Populates _instance_id (and channel/guild) for the embedded
# session. The default reads Discord's browser query string; a test fixture
# overrides it to inject an id so the connect path runs with no browser.
func _resolve_instance() -> void:
	if _is_browser():
		_read_query_params()


# Connects the SDK dispatch signals this service surfaces. Safe before auth: the
# signals stay quiet until _subscribe_activity_events actually subscribes.
func _connect_sdk_signals() -> void:
	_sdk.dispatch_error.connect(_on_sdk_error)
	_sdk.dispatch_activity_instance_participants_update.connect(_on_participants_update)
	_sdk.dispatch_activity_layout_mode_update.connect(
		func(data: DiscordSDK.ActivityLayoutModeUpdateData) -> void:
			layout_changed.emit(data.layout_mode)
	)
	_sdk.dispatch_orientation_update.connect(
		func(data: DiscordSDK.OrientationUpdateData) -> void:
			orientation_changed.emit(data.screen_orientation)
	)


# Subscribes to only the events this service surfaces. The SDK's bulk
# subscribe_to_events() also subscribes to voice/speaking/thermal/entitlement
# events that need OAuth scopes we never request, so Discord rejects each one.
func _subscribe_activity_events() -> void:
	if _sdk == null:
		return
	_sdk.subscribe_event("ACTIVITY_INSTANCE_PARTICIPANTS_UPDATE")
	_sdk.subscribe_event("ACTIVITY_LAYOUT_MODE_UPDATE")
	_sdk.subscribe_event("ORIENTATION_UPDATE")


# Static, lazy embed detection installed through
# NetwService.set_transport_restricted_probe, so
# WebRTC/native directories (WebTorrent, Steam) stay dormant in the iframe. It
# checks the browser instance_id query. Called when a directory checks
# _should_register at enter-tree, by which time JavaScriptBridge is live, so it
# never runs JS at class-load time.
static func _detect_embedded() -> bool:
	if OS.has_feature("web") or OS.get_name() == "Web":
		return "instance_id=" in String(JavaScriptBridge.eval("window.location.search", true))
	return false


# Fetches the current roster once and emits roster_changed. A failed fetch leaves
# the cached roster untouched, so a transient error never clears a good roster.
func _refresh_roster() -> void:
	if _sdk == null:
		return
	var res := await _sdk.command_get_instance_connected_participants()
	if res == null:
		return
	_participants = res.participants
	roster_changed.emit(_participants)


func _on_participants_update(data: DiscordSDK.ParticipantsUpdateData) -> void:
	_participants = data.participants
	roster_changed.emit(_participants)


func _on_sdk_error(data: DiscordSDK.ErrorEventData) -> void:
	activity_failed.emit("Discord error %d: %s" % [data.code, data.message])


# Posts the OAuth code to the token endpoint and returns the access token, or an
# empty string on failure. The endpoint is resolved against the page origin so
# the request hits the Discord-mapped Worker.
func _exchange_code(code: String) -> String:
	var url := _absolute_token_url()
	if url.is_empty():
		push_error("DiscordActivityService: oauth token endpoint unset.")
		return ""
	var request := HTTPRequest.new()
	add_child(request)
	var headers := PackedStringArray(["Content-Type: application/json"])
	var body := JSON.stringify({ "code": code })
	var err := request.request(url, headers, HTTPClient.METHOD_POST, body)
	if err != OK:
		request.queue_free()
		push_error(
			"DiscordActivityService: token request failed to start: %s" % [err]
		)
		return ""
	var result: Array = await request.request_completed
	request.queue_free()
	# result = [result, response_code, headers, body]
	var bytes: PackedByteArray = result[3]
	var parsed: Variant = JSON.parse_string(bytes.get_string_from_utf8())
	if typeof(parsed) != TYPE_DICTIONARY:
		return ""
	return String(parsed.get("access_token", ""))


func _absolute_token_url() -> String:
	if token_endpoint.is_empty():
		return ""
	if token_endpoint.begins_with("http"):
		return token_endpoint
	var path := _proxied_path(token_endpoint)
	if _is_browser():
		var origin := String(JavaScriptBridge.eval("window.location.origin", true))
		return origin + path
	return path


# Maps a bare dev-portal path to the path Discord actually serves it at. Discord
# serves the activity behind a fixed /.proxy/ namespace, so /token is reached at
# /.proxy/token. Accepts a bare or slash-prefixed path and is idempotent when the
# /.proxy/ prefix is already present.
func _proxied_path(p: String) -> String:
	var trimmed := p.trim_prefix("/")
	if trimmed.begins_with(".proxy/"):
		return "/" + trimmed
	return "/.proxy/" + trimmed


func _is_browser() -> bool:
	return OS.has_feature("web") or OS.get_name() == "Web"


# Records the new lifecycle state and announces the transition. A no-op when the
# state is unchanged so a re-entrant start() never emits a spurious transition.
func _set_state(to: State) -> void:
	if to == state:
		return
	var from := state
	state = to
	state_changed.emit(from, to)


# Resolves the instance id once through _resolve_instance. Memoized so
# _should_register and the public getters agree.
func _detect() -> void:
	if _detected:
		return
	_detected = true
	_resolve_instance()


# Pushes the resolved participant id onto the rendezvous so each participant
# authenticates to its backend as a distinct user. Without this, two instances on
# one machine both default to the same backend user, which breaks the relay. The
# raw id is pushed and the backend normalizes it (see
# NakamaDiscordRendezvous.nakama_device_id). A no-op for a backend with no
# device_id (the dedicated WSS path) or when no id was resolved.
func _push_identity() -> void:
	if rendezvous == null:
		return
	if _device_id.is_empty():
		return
	if "device_id" in rendezvous:
		rendezvous.set("device_id", _device_id)


# Reads instance_id (and channel/guild) from the browser query string. Real
# Discord supplies all three; authenticate() resolves the device id from the
# signed-in user afterward.
func _read_query_params() -> void:
	var search := String(JavaScriptBridge.eval("window.location.search", true))
	for part in search.trim_prefix("?").split("&", false):
		var kv := part.split("=")
		if kv.size() != 2:
			continue
		match kv[0]:
			"instance_id":
				_instance_id = kv[1]
			"channel_id":
				_channel_id = kv[1]
			"guild_id":
				_guild_id = kv[1]
