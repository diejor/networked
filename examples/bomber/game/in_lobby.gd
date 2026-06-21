## In-lobby UI: member roster + start/leave actions.
##
## Bound primarily to [NetwTree] / [NetwContext]: the roster comes from
## accepted [signal NetwTree.player_joined] data, and the host-only "Start"
## button is gated on [method NetwTree.is_listen_server]. The [LobbyDirectory]
## is only consulted for resolving display names and leaving the social lobby.
extends Control

signal start_requested()

@onready var _title: Label = %LobbyTitleLabel
@onready var _member_list: ItemList = %MemberList
@onready var _start_btn: Button = %StartButton
@onready var _leave_btn: Button = %LeaveButton

var _directory: LobbyDirectory
var _ctx: NetwContext


func setup(directory: LobbyDirectory, ctx: NetwContext) -> void:
	_directory = directory
	_ctx = ctx

	_ctx.tree.peer_connected.connect(func(_id: int) -> void: refresh())
	_ctx.tree.peer_disconnected.connect(func(_id: int) -> void: refresh())
	_ctx.tree.player_joined.connect(func(_rj: ResolvedJoin) -> void: refresh())

	_start_btn.pressed.connect(func() -> void: start_requested.emit())
	_leave_btn.pressed.connect(_on_leave_pressed)

	set_title("")
	refresh()


func set_title(text: String) -> void:
	_title.text = text if not text.is_empty() else "Lobby"


func refresh() -> void:
	_member_list.clear()
	if _ctx == null:
		return
	var mp := multiplayer
	var local_id: int = mp.get_unique_id() if mp.multiplayer_peer else 0
	var joined_players := _ctx.tree.joined_players
	joined_players.sort_custom(
		func(a: ResolvedJoin, b: ResolvedJoin) -> bool:
			return a.peer_id < b.peer_id
	)
	for rj: ResolvedJoin in joined_players:
		var pid := rj.peer_id
		var label := str(rj.username)
		if pid == local_id:
			label += "   (you)"
		_member_list.add_item(label)

	var host := _ctx.tree.is_listen_server()
	_start_btn.visible = host
	_start_btn.disabled = not host


func _on_leave_pressed() -> void:
	if _directory:
		_directory.leave_lobby()
	if _ctx:
		await _ctx.tree.disconnect_player()
