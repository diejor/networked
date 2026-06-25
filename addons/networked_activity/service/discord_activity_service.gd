## Session service that runs a [MultiplayerTree] as an embedded Discord Activity.
##
## Everything inside the Godot client that is identical across Discord Activities
## lives here: detecting that the game is embedded, driving the Discord SDK
## handshake to ready, resolving the local player's Discord identity, and turning
## the shared [code]instance_id[/code] into a live session through a pluggable
## [DiscordRendezvous]. The service owns the platform layer only and never names a
## transport backend. The chosen [DiscordRendezvous] owns the backend, so the same
## service drives a [NakamaDiscordRendezvous] relay or a
## [DedicatedDiscordRendezvous] WSS server unchanged. The game owns only the
## deployment (a relay or WSS server, a token Worker) and which rendezvous to use.
##
## The service registers only when the game is actually embedded
## ([method in_discord]), so a normal desktop or web build keeps its usual
## backends untouched.
## [codeblock]
## MultiplayerTree
## +-- DiscordActivityService   (client_id, token_endpoint, rendezvous)
##     +-- DiscordRendezvous installs its transport seams from bind()
##
## await service.start()                    # SDK handshake -> ready
## await service.authenticate()             # OAuth -> DiscordUser
## await service.connect_activity(payload)  # resolve instance -> host or join
## [/codeblock]
## [method start] and [method authenticate] drive the browser Discord SDK, so they
## are no-ops when no SDK is present. [method connect_activity] still runs because
## the rendezvous only needs the [member instance_id].
class_name DiscordActivityService
extends NetwService

## Emitted once the Discord SDK handshake reaches ready (or immediately when there
## is no SDK to handshake). [method connect_activity] is safe to call after this.
signal activity_ready()

## Emitted after [method authenticate] resolves the local Discord identity,
## carrying the [DiscordSDK.DiscordUser].
signal identity_resolved(user: DiscordSDK.DiscordUser)

## Emitted when a step fails in a way the game should surface rather than crash
## on, carrying a short [param reason]. Also carries Discord-side errors relayed
## from [signal DiscordSDK.dispatch_error].
signal activity_failed(reason: String)

## Emitted whenever the Discord participant roster changes, carrying the current
## [Array] of [DiscordSDK.DiscordUser]. Driven by an initial fetch after
## [method start] and by
## [signal DiscordSDK.dispatch_activity_instance_participants_update]. Reconcile
## it against networked peers through the shared device id (a participant's
## [member DiscordSDK.DiscordSimpleUser.id] equals the backend user id set by
## [method authenticate], so [method device_id] correlates the two). See
## [method participants].
signal roster_changed(participants: Array)

## Emitted when Discord changes the activity layout mode (focused, PIP, grid),
## carrying the raw [param layout_mode]. A thin pass-through of
## [signal DiscordSDK.dispatch_activity_layout_mode_update].
signal layout_changed(layout_mode: int)

## Emitted when the device orientation changes, carrying the raw
## [param screen_orientation]. A thin pass-through of
## [signal DiscordSDK.dispatch_orientation_update].
signal orientation_changed(screen_orientation: int)

## Emitted on every [enum State] transition, carrying the [param from] and
## [param to] states. The single hook a game subscribes to in order to drive its
## own UI off the activity lifecycle (showing a connecting spinner, a rematch
## prompt, and so on). The finer-grained signals above fire alongside the matching
## transition.
signal state_changed(from: State, to: State)

## Emitted when a [constant State.CONNECTED] session ends underneath the activity,
## carrying a short [param reason] (the host left, the transport closed). The
## state moves to [constant State.DISCONNECTED] just before this fires. The game
## owns the policy: ignore it, show a prompt, or call [method reconnect] to claim
## or rejoin the same [member instance_id]. A graceful local
## [method MultiplayerTree.disconnect_player] does not emit this.
signal session_lost(reason: String)

## Lifecycle of the activity, from detecting the embed through a live session.
## [method connect_activity] walks IDLE through [constant CONNECTED]; a session
## that drops underneath a connected activity moves to [constant DISCONNECTED].
enum State {
	## Constructed, not yet handshaken. The state before [method start].
	IDLE,
	## The SDK handshake reached ready (or there was no SDK). [method start] done.
	READY,
	## OAuth resolved the local [DiscordSDK.DiscordUser]. [method authenticate] done.
	AUTHENTICATED,
	## A connect attempt is in flight inside [method connect_activity].
	CONNECTING,
	## The tree is online in the instance's shared session.
	CONNECTED,
	## A live session ended underneath the activity (host left, transport closed).
	## [method reconnect] returns to [constant CONNECTING] from here.
	DISCONNECTED,
}

## Current lifecycle [enum State]. Read it to branch without subscribing, or
## subscribe to [signal state_changed] for transitions.
var state: State = State.IDLE

## Discord application (client) id. Required for the SDK handshake and OAuth.
@export var client_id: String = ""

@export_group("OAuth")

## URL mapping used by [method authenticate] to exchange a Discord OAuth code
## for an access token.
##
## In Discord's developer portal, this should point to the Worker endpoint that
## receives [code]{"code": "..."}[/code] and returns a Discord
## [code]access_token[/code]. The default [code]token[/code] means the service
## posts to the activity proxy path [code]/.proxy/token[/code]. Use an absolute
## [code]http(s)://[/code] URL only for local testing or custom deployments that
## do not use Discord URL mappings.
@export var token_endpoint: String = "token"

## OAuth scopes requested by [method authenticate].
##
## [code]identify[/code] lets [method DiscordSDK.command_authenticate] resolve
## [member user]. Keep it unless the activity does not need
## [signal identity_resolved]. Add scopes only when the Worker and Discord
## developer portal are configured for the extra permission.
@export var scopes: Array[String] = ["identify", "rpc.activities.write"]

@export_group("")

## Resolves the shared [code]instance_id[/code] into a transport.
##
## Assign this explicitly. The service reports [constant ERR_UNCONFIGURED] from
## [method connect_activity] when no rendezvous is configured.
@export var rendezvous: DiscordRendezvous

## When [code]true[/code], asks Discord to encourage hardware acceleration right
## after the handshake reaches ready.
@export var encourage_hw_accel: bool = true

# Resolved local identity, set by authenticate(). Null until then.
var user: DiscordSDK.DiscordUser

var _sdk: DiscordSDK
var _instance_id: String = ""
var _channel_id: String = ""
var _guild_id: String = ""
var _device_id: String = ""
var _detected: bool = false
var _started: bool = false

# Payload from the last connect_activity, replayed by reconnect().
var _last_payload: JoinPayload
# Reason from the most recent server_disconnecting, carried into session_lost.
var _disconnect_reason: String = ""

# Last roster reported by Discord, newest wins. Backs participants().
var _participants: Array = []


# Wires the core transport-restriction probe as soon as this script loads, which
# (because a scene loads all its scripts before instancing any node) happens
# before any directory checks should_register. So WebRTC/native directories see
# the embed verdict and stay dormant in the iframe. A build that never references
# this class never loads it, so a normal game pays nothing.
static func _static_init() -> void:
	NetwService.transport_restricted_probe = _detect_embedded


## Returns [code]true[/code] only when the game is embedded in a Discord Activity,
## which is the same condition under which this service registers.
func in_discord() -> bool:
	_detect()
	return not _instance_id.is_empty()


## Returns the shared Discord instance id, the rendezvous key every participant
## sees. Available before the SDK is ready.
func instance_id() -> String:
	_detect()
	return _instance_id


## Returns the Discord voice channel id, or an empty string outside a browser.
func channel_id() -> String:
	return _channel_id


## Returns the Discord guild id, or an empty string in a DM or outside a browser.
func guild_id() -> String:
	return _guild_id


## Returns the stable participant id the local player authenticates with, pushed
## onto the [member rendezvous] so each participant is a distinct backend user.
##
## Resolved from the Discord user id by [method authenticate], so the same Discord
## user reconnects as the same backend user. Empty before authentication.
func device_id() -> String:
	return _device_id


# Registration is bound to actually being embedded, so a normal build keeps its
# usual backends and never pays for the Discord path.
func should_register() -> bool:
	return in_discord()


func service_entered(mt: MultiplayerTree) -> void:
	if rendezvous == null:
		Netw.dbg.warn("DiscordActivityService: rendezvous unset.")
	else:
		# The rendezvous owns its transport, so it installs whatever core seams
		# that backend needs. The service never reaches into a backend itself.
		rendezvous.bind(mt)
	var nakama_auth := mt.auth_provider as NakamaAuth
	if nakama_auth != null:
		# get_nakama_session() add_childs the session node, which fails if the
		# tree is still setting up its children when this service registers.
		# Defer past setup; the bind only stores references used later at connect.
		_bind_nakama_auth.call_deferred(mt, nakama_auth)
	_push_identity()
	# Bridge a dropped connection into the activity lifecycle so a host that leaves
	# (or any transport drop) surfaces as session_lost while connected, the
	# transition a game hangs its rematch policy on. server_disconnected fires only
	# on an involuntary drop, never on a graceful local disconnect_player, so it is
	# the precise loss signal; server_disconnecting carries the announced reason.
	mt.server_disconnecting.connect(
		func(reason: String) -> void:
			_disconnect_reason = reason
	)
	mt.server_disconnected.connect(_on_session_dropped)
	# The SDK is parented in start(), not here: add_child during service setup
	# fails ("parent busy"), and the SDK's _ready() must run (it sets up the JS
	# bridge) before start() calls init(), or init() no-ops and ready() hangs.
	_sdk = _create_sdk()
	if _sdk != null:
		_sdk.name = &"DiscordSDK"


# Binds the verified-identity provider to the shared session and tree once the
# tree has finished setting up, so get_nakama_session() can add_child safely.
func _bind_nakama_auth(mt: MultiplayerTree, nakama_auth: NakamaAuth) -> void:
	nakama_auth.bind_session(mt.get_nakama_session())
	nakama_auth.bind_tree(mt)


## Drives the Discord SDK handshake to ready and returns whether it succeeded.
##
## With no SDK present there is nothing to handshake, so this records the started
## state and reports success immediately. With an SDK it runs
## [method DiscordSDK.init] then awaits [method DiscordSDK.ready], optionally
## encouraging hardware acceleration. Emits [signal activity_ready] on success and
## [signal activity_failed] otherwise. Safe to call more than once.
func start() -> bool:
	if _started:
		return true
	_detect()
	if _sdk == null:
		# No browser SDK to handshake. The connect path still runs because the
		# rendezvous only needs the instance id.
		_started = true
		_set_state(State.READY)
		Netw.dbg.info("DiscordActivityService: ready, no SDK (instance %s).", [_instance_id])
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
	Netw.dbg.info("DiscordActivityService: ready (instance %s).", [_instance_id])
	activity_ready.emit()
	return true


## Resolves the local Discord identity through the OAuth token exchange.
##
## Runs [method DiscordSDK.command_authorize] for an OAuth code, exchanges it at
## [member token_endpoint] for an access token, then
## [method DiscordSDK.command_authenticate]s to obtain the
## [DiscordSDK.DiscordUser]. [member scopes] controls what
## [method DiscordSDK.command_authorize] asks Discord for. Stores the resolved
## user on [member user] and emits [signal identity_resolved]. With no SDK there
## is no Discord account to resolve, so this returns [code]false[/code].
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
	Netw.dbg.info("DiscordActivityService: identity %s resolved.", [user.global_name])
	identity_resolved.emit(user)
	# Subscribe and fetch the roster now that the session is authenticated.
	# Discord rejects both before authorize completes, so doing this in start()
	# produced a wall of SUBSCRIBE errors.
	_subscribe_activity_events()
	_refresh_roster()
	return true


## Connects into the instance's shared session through the [member rendezvous].
##
## Ensures [method start] ran, then hands the [member rendezvous] the
## [member instance_id], the tree, and [param join_payload] through
## [method DiscordRendezvous.connect_session]. The rendezvous owns the whole
## host-versus-join decision and any concurrent-launch reconciliation, so this
## only walks the [enum State] machine ([constant State.CONNECTING] then
## [constant State.CONNECTED] on success) and surfaces a failure through
## [signal activity_failed]. The payload is remembered so [method reconnect] can
## reuse it after a [signal session_lost]. Returns the [enum Error] from the
## rendezvous.
func connect_activity(join_payload: JoinPayload) -> Error:
	if not await start():
		return ERR_UNAVAILABLE
	var mt := MultiplayerTree.resolve(self)
	if mt == null:
		return ERR_UNCONFIGURED
	if rendezvous == null:
		var reason := "DiscordActivityService: rendezvous unset."
		Netw.dbg.warn(reason)
		activity_failed.emit(reason)
		return ERR_UNCONFIGURED
	_push_identity()
	_last_payload = join_payload
	_set_state(State.CONNECTING)
	var err := await rendezvous.connect_session(_instance_id, mt, join_payload)
	if err != OK:
		activity_failed.emit("Activity connect failed: %s" % error_string(err))
		_set_state(State.DISCONNECTED)
		return err
	_set_state(State.CONNECTED)
	return OK


## Reconnects into the same [member instance_id] after a [signal session_lost],
## reusing the payload from the last [method connect_activity].
##
## The default recovery a game can wire straight to a "rematch" button: it re-runs
## [method connect_activity], so the freshest-record self-heal lets this
## participant claim the instance as the new host when the old host is gone, or
## join whoever already did. Returns [constant ERR_UNCONFIGURED] when no prior
## connect supplied a payload to reuse.
func reconnect() -> Error:
	if _last_payload == null:
		return ERR_UNCONFIGURED
	return await connect_activity(_last_payload)


## Returns the most recent Discord participant roster as an [Array] of
## [DiscordSDK.DiscordUser], newest update wins.
##
## Updated by [signal roster_changed]. Empty before the first update. Correlate an
## entry to a networked peer through the shared device id (see [method device_id]).
func participants() -> Array:
	return _participants


## Sets the activity's Discord rich presence from a short [param state] and
## [param details] line.
##
## A no-op with no SDK and before [method start] reaches ready, so a game can call
## it unconditionally. A thin pass-through of
## [method DiscordSDK.command_set_activity].
func set_presence(state: String, details: String) -> void:
	if _sdk == null or not _started:
		return
	await _sdk.command_set_activity(state, details)


## Opens Discord's invite dialog so the local player can pull others into this
## activity instance. A no-op with no SDK. A thin pass-through of
## [method DiscordSDK.command_open_invite_dialog].
func open_invite_dialog() -> void:
	if _sdk == null:
		return
	await _sdk.command_open_invite_dialog()


## Opens Discord's share-moment dialog for [param media_url]. A no-op with no SDK.
## A thin pass-through of [method DiscordSDK.command_open_share_moment_dialog].
func share_moment(media_url: String) -> void:
	if _sdk == null:
		return
	await _sdk.command_open_share_moment_dialog(media_url)


## Opens [param url] in the player's external browser through Discord, which
## blocks raw navigation out of the iframe. A no-op with no SDK. A thin
## pass-through of [method DiscordSDK.command_open_external_link].
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


# Static, lazy embed detection backing NetwService.transport_restricted_probe, so
# WebRTC/native directories (WebTorrent, Steam) stay dormant in the iframe. It
# checks the browser instance_id query. Called when a directory checks
# should_register at enter-tree, by which time JavaScriptBridge is live, so it
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
		Netw.dbg.error("DiscordActivityService: oauth token endpoint unset.")
		return ""
	var request := HTTPRequest.new()
	add_child(request)
	var headers := PackedStringArray(["Content-Type: application/json"])
	var body := JSON.stringify({ "code": code })
	var err := request.request(url, headers, HTTPClient.METHOD_POST, body)
	if err != OK:
		request.queue_free()
		Netw.dbg.error("DiscordActivityService: token request failed to start: %s", [err])
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


# Bridges an involuntary connection drop into the activity lifecycle. Only a drop
# underneath a live (CONNECTED) session is a loss the game should react to; a drop
# reported from any other state is noise from an ordinary connect/leave flow.
func _on_session_dropped() -> void:
	if state != State.CONNECTED:
		return
	_set_state(State.DISCONNECTED)
	var reason := _disconnect_reason if not _disconnect_reason.is_empty() else "host left"
	_disconnect_reason = ""
	session_lost.emit(reason)


# Resolves the instance id once through _resolve_instance. Memoized so
# should_register and the public getters agree.
func _detect() -> void:
	if _detected:
		return
	_detected = true
	_resolve_instance()


# Pushes the resolved participant id onto the rendezvous so each participant
# authenticates to its backend as a distinct user. Without this, two instances on
# one machine both default to the same backend user, which breaks the relay. The
# raw id is pushed and the backend normalizes it (see
# NakamaDiscordRendezvous._normalized_device_id). A no-op for a backend with no
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
