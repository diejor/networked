class_name BomberPlayerSpawner
extends MultiplayerSpawner

const PLAYER_SCENE := preload("res://examples/bomber/game/player.tscn")

@onready var level: Node = Netw.scene(self).root
@onready var players_root: Node = get_node(spawn_path)


func _ready() -> void:
	spawn_function = make_player


func spawn_participant(participant: NetwParticipant, slot: int) -> void:
	assert(multiplayer.is_server())
	if NetwEntity.find(players_root, participant) != null:
		return
	spawn(
		{
			peer_id = participant.peer_id,
			spawn_index = slot,
			username = participant.username,
		},
	)


func make_player(data: Dictionary) -> Node:
	var player := PLAYER_SCENE.instantiate()
	var peer_id := int(data.peer_id)
	var username := str(data.username)
	var spawn_index := int(data.spawn_index)
	NetwEntity.bind(player, StringName(username), peer_id)

	var score := level.get_node("Score")
	score.add_player(peer_id, username)

	player.position = spawn_position(spawn_index)

	var label := player.get_node("%label") as Label
	label.text = username

	return player


func spawn_position(spawn_index: int) -> Vector2:
	var spawn_points := level.get_node("SpawnPoints")
	var point_index := spawn_index % spawn_points.get_child_count()
	var marker := spawn_points.get_child(point_index) as Node2D
	return marker.position if marker else Vector2.ZERO
