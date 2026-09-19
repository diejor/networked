extends CanvasLayer

@onready var status: Label = %StatusLabel
@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var session: NetwSessionHandle = Netw.session(self)

var activity: DiscordActivityService


func _ready() -> void:
	session.presentation_changed.connect(on_presentation_changed)
	session.disconnecting.connect(on_server_disconnecting)
	session.disconnected.connect(on_server_disconnected)

	var gamestate := Netw.service(self, BomberGamestate) as BomberGamestate
	gamestate.game_error.connect(on_game_error)

	status.visible = false

	var discord := Netw.service(self, DiscordActivityService) \
			as DiscordActivityService
	if discord != null and discord.in_discord():
		activity = discord
		@warning_ignore("missing_await")
		enter_discord_activity(discord)
		return

	show_browser()


func on_presentation_changed(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	if to != null:
		set_status("")


func show_browser() -> void:
	browser.visible = true


func enter_discord_activity(discord: DiscordActivityService) -> void:
	browser.visible = false
	set_status("Connecting to Discord Activity...")

	discord.session_lost.connect(on_activity_session_lost)

	if not await discord.start():
		set_status("Discord handshake failed.")
		return
	await discord.authenticate()

	var err := await discord.connect_activity(
		StringName(discord_username(discord)),
	)
	if err != OK:
		set_status("Activity connect failed: %s" % error_string(err))


func discord_username(discord: DiscordActivityService) -> String:
	if discord.user != null and not discord.user.global_name.is_empty():
		return discord.user.global_name
	var did := discord.device_id()
	return did if not did.is_empty() else "Player"


func on_server_disconnecting(_reason: String) -> void:
	if activity != null:
		return
	show_browser()


func on_server_disconnected() -> void:
	if activity != null:
		return
	show_browser()


func on_activity_session_lost(reason: String) -> void:
	set_status("Host left (%s). Reconnecting..." % reason)
	var err := await activity.reconnect()
	if err != OK:
		set_status("Reconnect failed: %s" % error_string(err))


func on_game_error(text: String) -> void:
	set_status(text)
	if not session.is_online:
		show_browser()


func set_status(text: String) -> void:
	status.text = text
	status.visible = not text.is_empty()
