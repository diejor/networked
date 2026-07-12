## Bomber pre-session shell: owns the server browser and a status overlay.
##
## In-session UI ships inside its own scene (the lobby roster lives in
## lobby_level.tscn), so the shell only decides whether the pre-session
## [ConnectBrowser] is shown, keyed on the local participant's scene membership.
extends CanvasLayer

@onready var _status: Label = %StatusLabel
@onready var _browser: ConnectBrowser = %ConnectBrowser
@onready var _ctx: NetwMultiplayer = Netw.of(self)

var _activity: DiscordActivityService


func _ready() -> void:
	# The browser is a descendant of the tree now, so it self-resolves its own
	# ConnectSession by ancestry (deferred, so it dodges the tree's child-setup
	# window). No bind() needed, and eager .connect access here would trip the
	# "parent busy" service assertion.
	_ctx.local_scene_changed.connect(_on_local_scene_changed)
	_ctx.session_ended.connect(_show_browser)
	_ctx.server_disconnecting.connect(_on_server_disconnecting)
	_ctx.server_disconnected.connect(_on_server_disconnected)

	var gamestate := _ctx.get_service(BomberGamestate) as BomberGamestate
	gamestate.game_error.connect(_on_game_error)

	_status.visible = false

	var activity := _ctx.get_service(DiscordActivityService) \
			as DiscordActivityService
	if activity != null and activity.in_discord():
		_activity = activity
		_enter_discord_activity(activity)
		return

	_show_browser()


# The shell shows the browser only when the local participant is in no scene.
# Every scene ships its own UI, so the browser steps aside on admission.
func _on_local_scene_changed(_from: NetwScene, to: NetwScene) -> void:
	_browser.visible = to == null
	if to != null:
		_set_status("")


func _show_browser() -> void:
	_browser.visible = true


func _enter_discord_activity(activity: DiscordActivityService) -> void:
	_browser.visible = false
	_set_status("Connecting to Discord Activity...")

	activity.session_lost.connect(_on_activity_session_lost)

	if not await activity.start():
		_set_status("Discord handshake failed.")
		return
	await activity.authenticate()

	var payload := JoinPayload.new()
	payload.username = _discord_username(activity)

	var err := await activity.connect_activity(payload)
	if err != OK:
		_set_status("Activity connect failed: %s" % error_string(err))


func _discord_username(activity: DiscordActivityService) -> String:
	if activity.user != null and not activity.user.global_name.is_empty():
		return activity.user.global_name
	var did := activity.device_id()
	return did if not did.is_empty() else "Player"


func _on_server_disconnecting(_reason: String) -> void:
	if _activity != null:
		return
	_show_browser()


func _on_server_disconnected() -> void:
	if _activity != null:
		return
	_show_browser()


func _on_activity_session_lost(reason: String) -> void:
	_set_status("Host left (%s). Reconnecting..." % reason)
	var err := await _activity.reconnect()
	if err != OK:
		_set_status("Reconnect failed: %s" % error_string(err))


func _on_game_error(text: String) -> void:
	_set_status(text)
	if not _ctx.is_online():
		_show_browser()


func _set_status(text: String) -> void:
	_status.text = text
	_status.visible = not text.is_empty()
