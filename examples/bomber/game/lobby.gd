## Bomber lobby shell.
##
## This script is the bridge: it owns the state swap and coordinates with
## [ConnectBrowser] to enter the session.
extends Control


enum State { PRE_LOBBY, IN_LOBBY }

@warning_ignore("unused_private_class_variable")
@onready var _title: Label = %TitleLabel
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

	_set_state(State.PRE_LOBBY)


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
	_back_to_pre_lobby()


func _on_server_disconnected() -> void:
	_back_to_pre_lobby()


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
