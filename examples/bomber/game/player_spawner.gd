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
		
		_register_score_player(rj)
		spawn({
			"peer_id": rj.peer_id,
			"spawn_index": index,
			"username": str(rj.username),
		})


func _spawn_player(data: Variant) -> Node:
	var spawn_data: Dictionary = data if data is Dictionary else {}
	var peer_id := int(spawn_data.get("peer_id", 0))
	var username := str(spawn_data.get("username", ""))
	var spawn_index := int(spawn_data.get("spawn_index", 0))
	
	var player := PLAYER_SCENE.instantiate()
	NetwEntity.bundle(player, peer_id, StringName(username))
	
	var spawn_position := _get_spawn_position(spawn_index)
	player.set("synced_position", spawn_position)
	
	var label := player.get_node("%label") as Label
	label.text = username
	
	return player


func _has_player(rj: ResolvedJoin) -> bool:
	var players_root := get_node_or_null(spawn_path)
	if not players_root:
		return false
	
	var node_name := NetwEntity.format_name(
		str(rj.username),
		rj.peer_id
	)
	return players_root.get_node_or_null(node_name) != null


func _register_score_player(rj: ResolvedJoin) -> void:
	var world := ctx.scene.get_level()
	
	var score := world.get_node("Score")
	score.add_player(rj.peer_id, str(rj.username))


func _get_spawn_position(spawn_index: int) -> Vector2:
	var world := ctx.scene.get_level()
	
	var spawn_points := world.get_node("SpawnPoints")
	
	var point_index := spawn_index % spawn_points.get_child_count()
	var marker := spawn_points.get_child(point_index) as Node2D
	return marker.position if marker else Vector2.ZERO
