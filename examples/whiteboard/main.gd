extends Node

const ROOM := preload("res://examples/whiteboard/room.tscn")

var room: Node


func _init() -> void:
	Netw.configure_session(self).app(&"whiteboard")
	Netw.configure_clock(self)
	Netw.configure_spawn(spawn_room)
	Netw.configure_join(enter)


func spawn_room() -> Node:
	return ROOM.instantiate()


func enter(player: NetwPlayer) -> void:
	if not is_instance_valid(room):
		room = Netw.spawn(spawn_room)
		add_child(room)
	Netw.scene(room).watch(player)
