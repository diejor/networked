class_name InLobby
extends Control

signal start_pressed()

@onready var member_list: ItemList = %MemberList
@onready var start_btn: Button = %StartButton
@onready var leave_btn: Button = %LeaveButton

@onready var session: NetwSession = Netw.session(self)


func _ready() -> void:
	hide()
	session.player_joined.connect(on_roster_changed)
	session.player_left.connect(on_roster_changed)
	session.local_joined.connect(on_local_joined)
	session.scene_ready.connect(on_scene_ready)
	session.exiting.connect(hide.unbind(2))
	start_btn.pressed.connect(start_pressed.emit)
	leave_btn.pressed.connect(session.leave)


func on_local_joined(_player: NetwPlayer) -> void:
	refresh()
	show()


func on_scene_ready(scene: NetwScene) -> void:
	hide()
	scene.root.tree_exited.connect(on_scene_closed)


func on_scene_closed() -> void:
	visible = session.is_online


func on_roster_changed(_player: NetwPlayer) -> void:
	refresh()


func refresh() -> void:
	member_list.clear()
	for player: NetwPlayer in session.players:
		if player.is_active:
			var you := "   (you)" if player == session.local_player else ""
			member_list.add_item("%s%s" % [player.username, you])
	start_btn.visible = session.role == NetwSession.ROLE_LISTEN_SERVER
