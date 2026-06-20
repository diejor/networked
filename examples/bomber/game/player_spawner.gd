class_name BomberPlayerSpawner
extends MultiplayerSpawner
## Spawns bomber player entities for accepted join payloads.

const PLAYER_SCENE := preload("res://examples/bomber/game/player.tscn")

@onready var ctx := Netw.ctx(self)
@onready var gamestate: BomberGamestate = ctx.services.get_service(BomberGamestate)


func _ready() -> void:
	spawn_function = _spawn_player
	if multiplayer.is_server():
		gamestate.match_started.connect(_on_match_started)


func _on_match_started() -> void:
	spawn_joined_players(ctx.tree.get_joined_players())


## Server-only. Spawns one player for each accepted join data.
func spawn_joined_players(joined_players: Array[ResolvedJoin]) -> void:
	assert(multiplayer.is_server())

	joined_players.sort_custom(
		func(a: ResolvedJoin, b: ResolvedJoin) -> bool:
			return a.peer_id < b.peer_id
	)

	for index in joined_players.size():
		var rj := joined_players[index]
		if _has_player(rj):
			continue

		var data := { spawn_index = index }
		spawn(NetwEntity.decorate_spawn(data, rj))


func _spawn_player(data: Dictionary) -> Node:
	var spawn_identity := NetwEntity.spawn_identity(data)

	var player := PLAYER_SCENE.instantiate()
	spawn_identity.bind(player)
	var username := str(spawn_identity.entity_id)
	var spawn_index := int(data.spawn_index)

	var world := ctx.scene.get_level()
	var score := world.get_node("Score")
	score.add_player(spawn_identity.peer_id, username)

	player.position = _get_spawn_position(spawn_index)

	var label := player.get_node("%label") as Label
	label.text = username

	return player


func _has_player(rj: ResolvedJoin) -> bool:
	var players_root := get_node_or_null(spawn_path)
	return NetwEntity.find(players_root, rj) != null if players_root else false


func _get_spawn_position(spawn_index: int) -> Vector2:
	var world := ctx.scene.get_level()

	var spawn_points := world.get_node("SpawnPoints")

	var point_index := spawn_index % spawn_points.get_child_count()
	var marker := spawn_points.get_child(point_index) as Node2D
	return marker.position if marker else Vector2.ZERO
