class_name InLobby
extends Control

@export_file("*.tscn") var match_scene: String

@onready var member_list: ItemList = %MemberList
@onready var start_btn: Button = %StartButton
@onready var leave_btn: Button = %LeaveButton

@onready var session: NetwSessionHandle = Netw.session(self)
@onready var lobby: NetwSceneHandle = NetwEntity.of(self).scene


func _ready() -> void:
	lobby.observe(NetwMultiplayer.SCENE_EVENT_PARTICIPANT, membership_edge)
	start_btn.pressed.connect(on_start_pressed)
	leave_btn.pressed.connect(session.leave)
	refresh()


func on_start_pressed() -> void:
	Netw.change_scene_to_file(self, match_scene)


func membership_edge(_present: bool, _participant: NetwParticipant) -> bool:
	refresh()
	return false


func refresh() -> void:
	member_list.clear()
	var local := session.local_participant
	var participants := lobby.participants
	participants.sort_custom(
		func(a: NetwParticipant, b: NetwParticipant) -> bool:
			return a.peer_id < b.peer_id
	)
	for participant: NetwParticipant in participants:
		var suffix := "   (you)" if participant == local else ""
		member_list.add_item("%s%s" % [participant.username, suffix])

	var host := session.role == NetwMultiplayer.ROLE_LISTEN_SERVER
	start_btn.visible = host
	start_btn.disabled = not host
