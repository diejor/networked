class_name InLobby
extends Control
## In-lobby roster and start/leave controls, scoped to the Lobby scene it ships in.
##
## Lives inside lobby_level.tscn, so it resolves its own [NetwMultiplayer] through
## [method Netw.of] and is created and freed with the scene. The roster reflects
## [member NetwSceneHandle.participants]; the host-only Start button moves everyone
## into the World scene through [BomberGamestate].

@onready var _member_list: ItemList = %MemberList
@onready var _start_btn: Button = %StartButton
@onready var _leave_btn: Button = %LeaveButton

@onready var _ctx := Netw.of(self)


func _ready() -> void:
	NetwEntity.of(self).scene \
			.on_participant_entered(_on_membership_changed) \
			.on_participant_left(_on_membership_changed)
	_start_btn.pressed.connect(_on_start_pressed)
	_leave_btn.pressed.connect(_ctx.session.leave)
	_refresh()


func _on_start_pressed() -> void:
	var gamestate := _ctx.get_service(BomberGamestate) as BomberGamestate
	gamestate.begin_game()


func _on_membership_changed(_participant: NetwParticipant) -> void:
	_refresh()


func _refresh() -> void:
	_member_list.clear()
	var local := _ctx.local_participant
	var scene: NetwSceneHandle = NetwEntity.of(self).scene
	var participants := scene.participants
	participants.sort_custom(
		func(a: NetwParticipant, b: NetwParticipant) -> bool:
			return a.peer_id < b.peer_id
	)
	for participant: NetwParticipant in participants:
		var suffix := "   (you)" if participant == local else ""
		_member_list.add_item("%s%s" % [participant.username, suffix])

	# Recomputed here (not only in _ready) because on a listen-server host the
	# role is assigned just after startup scenes spawn, so the first refresh
	# after admission is when the role becomes authoritative.
	var host := _ctx.role == NetwMultiplayer.Role.LISTEN_SERVER
	_start_btn.visible = host
	_start_btn.disabled = not host
