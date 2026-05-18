## Bomber lobby shell.
##
## Two-layer split, made visible in the scene tree:
##
## - [PreLobby] talks to [LobbyProvider] only. Social membership lives there:
##   browsing lobbies, creating, joining. The multiplayer session does not
##   exist yet.
## - [InLobby] talks to [NetwTree] / [NetwContext]. The roster, the host-only
##   Start button, and game start all live there. It only consults
##   [LobbyProvider] for [method LobbyProvider.get_member_name] and
##   [method LobbyProvider.leave_lobby] - the two genuinely cross-layer
##   operations.
##
## This script is the bridge: it owns the state swap and calls
## [method LobbyProvider.bind] at the boundary where the lobby layer hands
## the produced peer to the session layer.
extends Control

enum State { PRE_LOBBY, IN_LOBBY }

@warning_ignore("unused_private_class_variable")
@onready var _title: Label = %TitleLabel
@onready var _status: Label = %StatusLabel
@onready var _pre_lobby: Control = %PreLobby
@onready var _in_lobby: Control = %InLobby
@onready var gamestate: BomberGamestate = %Gamestate
@onready var multiplayer_tree: MultiplayerTree = %MultiplayerTree

@onready var _ctx: NetwContext = Netw.ctx(multiplayer_tree)

var _provider: LobbyProvider

var _state: State = State.PRE_LOBBY
var _pending_title: String = ""

func _ready() -> void:
	_provider = _ctx.services.get_service(LobbyProvider)
	assert(_provider)

	_pre_lobby.status_message.connect(_set_status)
	_pre_lobby.setup(_provider)
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


func _on_lobby_created(lobby_id: int) -> void:
	_pending_title = "Lobby %d (you)" % lobby_id
	_enter_lobby(lobby_id)


func _on_lobby_joined(lobby_id: int) -> void:
	_pending_title = "Lobby %d" % lobby_id
	_enter_lobby(lobby_id)


func _enter_lobby(_lobby_id: int) -> void:
	if _provider == null:
		return

	var pname := _provider.get_local_member_name()
	gamestate.player_name = pname
	
	var jp := JoinPayload.new()
	jp.username = pname
	
	var err := await _provider.bind(_ctx.tree, jp)
	if err != OK:
		_set_status("Bind failed: %s" % error_string(err))
		_pre_lobby.reset_buttons()
		return

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


func _back_to_pre_lobby() -> void:
	_pre_lobby.reset_buttons()
	_set_state(State.PRE_LOBBY)


func _set_state(s: State) -> void:
	_state = s
	_pre_lobby.visible = (s == State.PRE_LOBBY)
	_in_lobby.visible = (s == State.IN_LOBBY)


func _set_status(text: String) -> void:
	_status.text = text
	_status.visible = not text.is_empty()
