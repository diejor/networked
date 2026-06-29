## Bomber lobby shell.
##
## This script is the bridge: it owns the state swap and coordinates with
## [ConnectBrowser] to enter the session.
extends CanvasLayer

enum State { PRE_LOBBY, IN_LOBBY }

@warning_ignore("unused_private_class_variable")
@onready var _status: Label = %StatusLabel
@onready var _browser: ConnectBrowser = %ConnectBrowser
@onready var _in_lobby: Control = %InLobby
@onready var gamestate: BomberGamestate = %Gamestate
@onready var multiplayer_tree: MultiplayerTree = %MultiplayerTree

@onready var _ctx: NetwContext = Netw.ctx(multiplayer_tree)

var _directory: LobbyDirectory
var _connect: NetwConnect

var _state: State = State.PRE_LOBBY
var _pending_title: String = ""

# Set when embedded in a Discord Activity: the session_lost recovery owns the
# host-left case, so the generic server-disconnect handlers stand down.
var _activity: DiscordActivityService


func _ready() -> void:
	_connect = _ctx.connect

	# The directory is auto-discovered by ConnectSession; the lobby only needs
	# the handle for the in-lobby member list.
	_directory = _ctx.services.get_service(SteamLobbyDirectory)

	# Backends and templates are configured on the ConnectBrowser in lobby.tscn.
	_browser.bind(_connect)
	_connect.session_entered.connect(_on_browser_session_entered)

	_in_lobby.setup(_directory, _ctx)
	_in_lobby.start_requested.connect(_on_start_requested)

	_ctx.tree.server_disconnecting.connect(_on_server_disconnecting)
	_ctx.tree.server_disconnected.connect(_on_server_disconnected)

	gamestate.game_ended.connect(_on_game_ended)
	gamestate.game_error.connect(_on_game_error)
	gamestate.match_started.connect(_on_match_started)

	_status.visible = false

	# Embedded in a Discord Activity: skip the server browser and connect through
	# the rendezvous. The service only registers when actually embedded, so a
	# normal desktop or web build falls through to the usual pre-lobby browser.
	var activity := _ctx.services.get_service(DiscordActivityService) as DiscordActivityService
	if activity != null and activity.in_discord():
		_activity = activity
		_enter_discord_activity(activity)
		return

	_set_state(State.PRE_LOBBY)


# Drives the embedded-activity entry: handshake, best-effort identity, then
# connect_activity. Success drives the tree online, so the existing
# session_entered handler carries us into the in-lobby screen exactly like a
# browser host/join. Failure surfaces as a status message (no browser to fall
# back to inside Discord).
func _enter_discord_activity(activity: DiscordActivityService) -> void:
	_browser.visible = false
	_in_lobby.visible = false
	_set_status("Connecting to Discord Activity...")

	# Inside Discord there is no server browser to fall back to, so a dropped host
	# is recovered in place: claim or rejoin the same instance. This is the default
	# a game wires off the activity state machine; override it for richer UX.
	activity.session_lost.connect(_on_activity_session_lost)

	if not await activity.start():
		_set_status("Discord handshake failed.")
		return
	# A no-op when there is no SDK to authenticate against.
	await activity.authenticate()

	var payload := JoinPayload.new()
	payload.username = _discord_username(activity)

	var err := await activity.connect_activity(payload)
	if err != OK:
		_set_status("Activity connect failed: %s" % error_string(err))


# Resolves the local player name from the Discord identity, falling back to the
# device id and finally a placeholder.
func _discord_username(activity: DiscordActivityService) -> String:
	if activity.user != null and not activity.user.global_name.is_empty():
		return activity.user.global_name
	var did := activity.device_id()
	return did if not did.is_empty() else "Player"


# Transition to the InLobby screen when the server browser enters a session.
func _on_browser_session_entered() -> void:
	var local_id := multiplayer.get_unique_id()
	var rj := multiplayer_tree.get_joined_player(local_id)
	if rj:
		gamestate.player_name = rj.username

	if _pending_title.is_empty():
		if multiplayer_tree.role == MultiplayerTree.Role.LISTEN_SERVER:
			if multiplayer_tree.backend is SteamBackend:
				_pending_title = "Lobby %s (you)" % \
						multiplayer_tree.backend.get_join_address()
			else:
				_pending_title = "Direct Host (you)"
		else:
			if multiplayer_tree.backend is SteamBackend:
				_pending_title = "Lobby %s" % \
						multiplayer_tree.backend.get_join_address()
			else:
				_pending_title = "Direct Client"

	_in_lobby.set_title(_pending_title)
	_in_lobby.refresh()
	_set_state(State.IN_LOBBY)
	_set_status("")


func _on_start_requested() -> void:
	if _ctx.tree.is_listen_server():
		gamestate.begin_game()


func _on_server_disconnecting(_reason: String) -> void:
	# In Discord the session_lost recovery handles the drop; there is no pre-lobby
	# browser to return to.
	if _activity != null:
		return
	_back_to_pre_lobby()


func _on_server_disconnected() -> void:
	if _activity != null:
		return
	_back_to_pre_lobby()


# The host left the Discord Activity, so every other participant dropped. Re-enter
# the same instance: the freshest-record self-heal makes whoever reconnects first
# the new host and the rest join them, so the match resumes without a browser.
func _on_activity_session_lost(reason: String) -> void:
	_set_status("Host left (%s). Reconnecting..." % reason)
	show()
	var err := await _activity.reconnect()
	if err != OK:
		_set_status("Reconnect failed: %s" % error_string(err))


func _on_match_started() -> void:
	hide()


func _on_game_ended() -> void:
	show()
	if _ctx.tree.is_online():
		_in_lobby.refresh()
		_set_state(State.IN_LOBBY)
	else:
		_back_to_pre_lobby()


func _on_game_error(text: String) -> void:
	_set_status(text)
	show()
	if not _ctx.tree.is_online():
		_back_to_pre_lobby()


# Restores the pre-lobby state and refreshes the browser list.
func _back_to_pre_lobby() -> void:
	_pending_title = ""
	_connect.refresh()
	_set_state(State.PRE_LOBBY)


# Updates the active lobby screen view.
func _set_state(s: State) -> void:
	_state = s
	_browser.visible = (s == State.PRE_LOBBY)
	_in_lobby.visible = (s == State.IN_LOBBY)


# Updates the status message label display.
func _set_status(text: String) -> void:
	_status.text = text
	_status.visible = not text.is_empty()
