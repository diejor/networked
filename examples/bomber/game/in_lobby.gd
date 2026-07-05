class_name InLobby
extends Control
## In-lobby roster and start/leave controls, scoped to the Lobby scene it ships in.
##
## Lives inside lobby_level.tscn, so it resolves its own [NetwContext] through
## [method Netw.ctx] and is created and freed with the scene. The roster reflects
## [member NetwScene.participants]; the host-only Start button moves everyone into
## the World scene through [BomberGamestate].

@onready var _member_list: ItemList = %MemberList
@onready var _start_btn: Button = %StartButton
@onready var _leave_btn: Button = %LeaveButton

@onready var _ctx := Netw.ctx(self)


func _ready() -> void:
	_ctx.scene.participant_entered.connect(_on_membership_changed)
	_ctx.scene.participant_left.connect(_on_membership_changed)
	_start_btn.pressed.connect(_on_start_pressed)
	_leave_btn.pressed.connect(_ctx.tree.leave)
	_refresh()


func _on_start_pressed() -> void:
	var gamestate := _ctx.services.get_service(BomberGamestate) as BomberGamestate
	gamestate.begin_game()


func _on_membership_changed(_participant: NetwParticipant) -> void:
	_refresh()


func _refresh() -> void:
	_member_list.clear()
	var local := _ctx.tree.local_participant
	var participants := _ctx.scene.participants
	participants.sort_custom(
		func(a: NetwParticipant, b: NetwParticipant) -> bool:
			return a.peer_id < b.peer_id
	)
	for participant: NetwParticipant in participants:
		var suffix := "   (you)" if participant == local else ""
		_member_list.add_item("%s%s" % [participant.username, suffix])

	# Recomputed here (not only in _ready) because on a listen-server host the
	# role is assigned just after startup scenes spawn, so the first refresh
	# after admission is when is_listen_server() becomes authoritative.
	var host := _ctx.tree.is_listen_server()
	_start_btn.visible = host
	_start_btn.disabled = not host
