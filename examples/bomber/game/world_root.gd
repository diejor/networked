extends Node2D

const PLAYER_SCENE := preload("res://examples/bomber/game/player.tscn")


func _init() -> void:
	Netw.configure_scene(self).labeled(&"World")
	Netw.configure_spawn(spawn_player)


func spawn_player(username: StringName, peer_id: int, slot: int) -> Node:
	var player := PLAYER_SCENE.instantiate()
	player.position = spawn_position(slot)
	(player.get_node("%label") as Label).text = String(username)
	var seat: NetwPlayer = Netw.session(self).player_of(peer_id)
	get_node(^"Score").add_player(seat, String(username))
	return player


func spawn_position(slot: int) -> Vector2:
	var points := get_node(^"SpawnPoints")
	var marker := points.get_child(slot % points.get_child_count()) as Node2D
	return marker.position
