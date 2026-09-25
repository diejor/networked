extends "res://examples/playground/main.gd"

var stages := {}


func enter(player: NetwPlayer) -> void:
	var started := Time.get_ticks_usec()
	if not is_instance_valid(playground):
		playground = Netw.spawn(spawn_playground)
		var spawned := Time.get_ticks_usec()
		add_child(playground)
		var mounted := Time.get_ticks_usec()
		stages["world spawn"] = spawned - started
		stages["world mount"] = mounted - spawned
	var before_player := Time.get_ticks_usec()
	var root: Node = Netw.scene(playground).root
	var players: Node = root.get_node(^"Players")
	players.add_child(
		Netw.spawn_player(player, root.spawn_player, players.get_child_count())
	)
	var done := Time.get_ticks_usec()
	stages["player spawn and mount"] = done - before_player
	stages["enter total"] = done - started
