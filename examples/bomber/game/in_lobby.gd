## In-lobby UI: member roster + start/leave actions.
##
## Bound primarily to [NetwTree] / [NetwContext]: the roster comes from
## [signal NetwTree.peer_connected]/[signal NetwTree.peer_disconnected],
## and the host-only "Start" button is gated on
## [method NetwTree.is_listen_server]. The [LobbyProvider] is only consulted
## for the two genuinely cross-layer operations - resolving display names
## from peer ids, and leaving the social lobby on top of disconnecting the
## multiplayer session.
extends Control

signal start_requested()

@onready var _title: Label = %LobbyTitleLabel
@onready var _member_list: ItemList = %MemberList
@onready var _start_btn: Button = %StartButton
@onready var _leave_btn: Button = %LeaveButton

var _provider: LobbyProvider
var _ctx: NetwContext


func setup(provider: LobbyProvider, ctx: NetwContext) -> void:
	_provider = provider
	_ctx = ctx

	_ctx.tree.peer_connected.connect(func(_id: int) -> void: refresh())
	_ctx.tree.peer_disconnected.connect(func(_id: int) -> void: refresh())

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
	var ids: Array[int] = []
	if local_id != 0:
		ids.append(local_id)
	for pid: int in mp.get_peers():
		ids.append(pid)
	ids.sort()
	for pid in ids:
		var label: String = _provider.get_member_name(pid) if _provider \
			else "Player %d" % pid
		if pid == local_id:
			label += "   (you)"
		_member_list.add_item(label)

	var host := _ctx.tree.is_listen_server()
	_start_btn.visible = host
	_start_btn.disabled = not host


func _on_leave_pressed() -> void:
	if _provider:
		_provider.leave_lobby()
	if _ctx:
		await _ctx.tree.disconnect_player()
