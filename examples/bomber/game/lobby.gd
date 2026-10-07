extends CanvasLayer

@onready var status: Label = %StatusLabel
@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var in_lobby: InLobby = %InLobby
@onready var session: NetwSession = Netw.session(self)

var activity: DiscordActivityService


func _ready() -> void:
	session.scene_ready.connect(on_scene_ready)

	var gamestate := session.service(BomberGamestate) as BomberGamestate
	gamestate.game_error.connect(set_status)
	in_lobby.start_pressed.connect(gamestate.begin_game)

	status.visible = false

	var discord := session.service(DiscordActivityService) \
			as DiscordActivityService
	if discord != null and discord.in_discord():
		activity = discord
		@warning_ignore("missing_await")
		enter_discord_activity(discord)


func on_scene_ready(_scene: NetwScene) -> void:
	set_status("")


func enter_discord_activity(discord: DiscordActivityService) -> void:
	browser.visible = false
	set_status("Connecting to Discord Activity...")

	session.exited.connect(on_session_exited)

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


func on_session_exited(reason: NetwSession.ExitReason, detail: String) -> void:
	if reason == NetwSession.LOCAL_LEAVE:
		return
	set_status("Host left (%s). Reconnecting..." % detail)
	var err := await activity.connect_activity(
		StringName(discord_username(activity)),
	)
	if err != OK:
		set_status("Reconnect failed: %s" % error_string(err))


func set_status(text: String) -> void:
	status.text = text
	status.visible = not text.is_empty()
