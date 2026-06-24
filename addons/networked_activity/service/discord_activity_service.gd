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
## └── DiscordActivityService   (client_id, rendezvous = the pluggable backend)
##         rendezvous installs its own transport seams from bind()
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

## Discord application (client) id. Required for the SDK handshake and OAuth.
@export var client_id: String = ""

## URL the OAuth [code]code[/code] is exchanged at for an access token, relative
## to the page origin. Matches the Discord dev-portal URL mapping for the token
## Worker.
@export var token_endpoint: String = "/.proxy/token"

## OAuth scopes requested by [method authenticate].
@export var scopes: PackedStringArray = PackedStringArray([
	"identify", "guilds", "rpc.activities.write",
])

## Resolves the shared [code]instance_id[/code] into a transport. Defaults to a
## fresh [NakamaDiscordRendezvous] when left unset.
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
		rendezvous = NakamaDiscordRendezvous.new()
	# The rendezvous owns its transport, so it installs whatever core seams that
	# backend needs (the Nakama relay rewrites its socket through the iframe proxy
	# here). The service never reaches into a backend itself.
	rendezvous.bind(self, mt)
	_push_identity()
	_sdk = _create_sdk()
	if _sdk != null:
		_sdk.name = &"DiscordSDK"
		add_child(_sdk)


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
		Netw.dbg.info("DiscordActivityService: ready, no SDK (instance %s).", [_instance_id])
		activity_ready.emit()
		return true
	if client_id.is_empty():
		activity_failed.emit("Discord client_id unset")
		return false
	_sdk.init(client_id)
	await _sdk.ready()
	_instance_id = _sdk.instance_id
	_channel_id = _sdk.channel_id
	_guild_id = _sdk.guild_id
	if encourage_hw_accel:
		await _sdk.command_encourage_hardware_acceleration()
	_connect_sdk_signals()
	_started = true
	Netw.dbg.info("DiscordActivityService: ready (instance %s).", [_instance_id])
	activity_ready.emit()
	return true


## Resolves the local Discord identity through the OAuth token exchange.
##
## Runs [method DiscordSDK.command_authorize] for an OAuth code, exchanges it at
## [member token_endpoint] for an access token, then
## [method DiscordSDK.command_authenticate]s to obtain the
## [DiscordSDK.DiscordUser]. Stores it on [member user] and emits
## [signal identity_resolved]. With no SDK there is no Discord account to resolve,
## so this is a no-op returning [code]false[/code]. Identity is client-claimed in
## v1 and is not spoof-proof.
func authenticate() -> bool:
	if _sdk == null:
		return false
	if not await start():
		return false
	var authorize := await _sdk.command_authorize("code", Array(scopes), "")
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
	Netw.dbg.info("DiscordActivityService: identity %s resolved.", [user.global_name])
	identity_resolved.emit(user)
	# Subscribe and fetch the roster now that the session is authenticated.
	# Discord rejects both before authorize completes, so doing this in start()
	# produced a wall of SUBSCRIBE errors.
	_subscribe_activity_events()
	_refresh_roster()
	return true


## Resolves the instance into a session and connects, hosting when first and
## joining otherwise.
##
## Ensures [method start] ran, asks the [member rendezvous] to
## [method DiscordRendezvous.resolve] the [member instance_id] into a
## [JoinTarget], then hosts (empty address) or joins (match address). After a
## host it runs [method DiscordRendezvous.commit_host] and defers to the winner
## if a concurrent host won the claim. The hosted match is
## [constant LobbyDirectory.Visibility.PRIVATE] so Discord rooms never appear in
## a public lobby browse. Returns an [enum Error] from the underlying
## [method MultiplayerTree.join] or [method MultiplayerTree.host_player].
func connect_activity(join_payload: JoinPayload) -> Error:
	if not await start():
		return ERR_UNAVAILABLE
	var mt := MultiplayerTree.resolve(self)
	if mt == null:
		return ERR_UNCONFIGURED
	_push_identity()
	var target := await rendezvous.resolve(_instance_id, mt)
	if target == null:
		activity_failed.emit("Rendezvous could not resolve a transport")
		return ERR_CANT_RESOLVE

	if not target.address.is_empty():
		var join_err := await mt.join(target, join_payload)
		if join_err == OK:
			return OK
		# The recorded match is dead or unreachable. Become the new host and let
		# commit_host overwrite the stale record. join() leaves the tree OFFLINE
		# on failure, so hosting is safe.
		Netw.dbg.info(
			"DiscordActivityService: recorded match join failed (%s); hosting instead.",
			[error_string(join_err)],
		)
	return await _host_and_commit(mt, target, join_payload)


# Hosts privately, publishes the rendezvous record, and reconciles a concurrent
# host race by deferring to the winner. host_player uses the tree's backend (join
# sets it from the target, host does not), so seed it from the target template.
func _host_and_commit(
		mt: MultiplayerTree, target: JoinTarget, join_payload: JoinPayload,
) -> Error:
	mt.backend = target.make_backend_instance()
	var opts := LobbyDirectory.HostOptions.make(
		"Discord Activity", LobbyDirectory.Visibility.PRIVATE,
	)
	var host_err := await mt.host_player(join_payload, opts)
	if host_err != OK:
		return host_err
	var winner := await rendezvous.commit_host(_instance_id, mt)
	if winner == null:
		return OK
	# Lost the race: drop our match and join the winner's.
	await mt.disconnect_player()
	return await mt.join(winner, join_payload)


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
	var request := HTTPRequest.new()
	add_child(request)
	var url := _absolute_token_url()
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
	if token_endpoint.begins_with("http"):
		return token_endpoint
	if _is_browser():
		var origin := String(JavaScriptBridge.eval("window.location.origin", true))
		return origin + token_endpoint
	return token_endpoint


func _is_browser() -> bool:
	return OS.has_feature("web") or OS.get_name() == "Web"


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
			"instance_id": _instance_id = kv[1]
			"channel_id": _channel_id = kv[1]
			"guild_id": _guild_id = kv[1]
