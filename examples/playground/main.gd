extends Node

const PLAYGROUND := preload("res://examples/playground/playground.tscn")

var playground: Node


func _init() -> void:
	Netw.configure_session(self).app(&"cube-playground")
	Netw.configure_clock(self).ticks_per_second(60)
	Netw.configure_spawn(spawn_playground)
	Netw.configure_join(enter)


func spawn_playground() -> Node:
	return PLAYGROUND.instantiate()


func enter(player: NetwPlayer) -> void:
	if not is_instance_valid(playground):
		playground = Netw.spawn(spawn_playground)
		add_child(playground)
	var root: Node = Netw.scene(playground).root
	var players: Node = root.get_node(^"Players")
	players.add_child(
		Netw.spawn_player(player, root.spawn_avatar, players.get_child_count())
	)
