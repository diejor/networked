## Bomber lobby shell.
##
## Two-layer split, made visible in the scene tree:
##
## - [ConnectBrowser] handles direct connections and provider lobbies,
##   driven by a [ConnectSession] this script owns.
## - [InLobby] talks to [NetwTree] / [NetwContext]. The roster, the host-only
##   Start button, and game start all live there. It only consults
##   [LobbyProvider] for [method LobbyProvider.get_member_name] and
##   [method LobbyProvider.leave_lobby] - the two genuinely cross-layer
##   operations.
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

var _provider: LobbyProvider
var _session: ConnectSession

var _state: State = State.PRE_LOBBY
var _pending_title: String = ""

func _ready() -> void:
	_provider = _ctx.services.get_service(LobbyProvider)
	assert(_provider)

	_session = ConnectSession.new()
	add_child(_session)
	_session.bind_tree(multiplayer_tree)
	_session.register_provider(&"steam", _provider)
	_session.load_server_list()

	var ws_backend := WebSocketBackend.new()
	ws_backend.port = 10567
	_browser.backend_templates = [ws_backend]
	_browser.session = _session
	_session.session_entered.connect(_on_browser_session_entered)

	_in_lobby.setup(_provider, _ctx)
	_in_lobby.start_requested.connect(_on_start_requested)

	_provider.lobby_created.connect(_on_lobby_created)
	_provider.lobby_joined.connect(_on_lobby_joined)

	_ctx.tree.server_disconnecting.connect(_on_server_disconnecting)
	_ctx.tree.server_disconnected.connect(_on_server_disconnected)

	gamestate.game_ended.connect(_on_game_ended)
	gamestate.game_error.connect(_on_game_error)
	gamestate.match_started.connect(_on_match_started)

	_set_state(State.PRE_LOBBY)


# Triggered when the lobby provider reports a new lobby created by local host.
func _on_lobby_created(lobby_id: int) -> void:
	_pending_title = "Lobby %d (you)" % lobby_id


# Triggered when the lobby provider reports the local client joined a lobby.
func _on_lobby_joined(lobby_id: int) -> void:
	_pending_title = "Lobby %d" % lobby_id


# Transition to the InLobby screen when the server browser enters a session.
func _on_browser_session_entered() -> void:
	var local_id := multiplayer.get_unique_id()
	var rj := multiplayer_tree.get_joined_player(local_id)
	if rj:
		gamestate.player_name = rj.username

	if _pending_title.is_empty():
		if multiplayer_tree.role == MultiplayerTree.Role.LISTEN_SERVER:
			_pending_title = "Direct Host (you)"
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
	_session.refresh()
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
