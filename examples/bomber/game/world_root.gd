extends Node2D

const PLAYER_SCENE := preload("res://examples/bomber/game/player.tscn")


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"World")
	Netw.configure_spawn(spawn_player)


func _ready() -> void:
	if not multiplayer.is_server():
		return
	var gamestate := Netw.service(self, BomberGamestate) as BomberGamestate
	gamestate.open_match(Netw.scene(self))


func spawn_player(username: StringName, peer_id: int, slot: int) -> Node:
	var player := PLAYER_SCENE.instantiate()
	player.position = spawn_position(slot)
	(player.get_node("%label") as Label).text = String(username)
	get_node(^"Score").add_player(peer_id, String(username))
	return player


func spawn_position(slot: int) -> Vector2:
	var points := get_node(^"SpawnPoints")
	var marker := points.get_child(slot % points.get_child_count()) as Node2D
	return marker.position
