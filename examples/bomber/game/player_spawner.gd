class_name BomberPlayerSpawner
extends MultiplayerSpawner
## Spawns bomber player entities for accepted join payloads.

const PLAYER_SCENE := preload("res://examples/bomber/game/player.tscn")

@onready var ctx := Netw.ctx(self)
@onready var gamestate: BomberGamestate = ctx.services.get_service(BomberGamestate)


func _ready() -> void:
	spawn_function = NetwEntity.wrap_spawn(_spawn_player)
	if multiplayer.is_server():
		ctx.scene.participant_entered.connect(_on_participant_entered)
		for participant: NetwParticipant in ctx.scene.participants:
			_on_participant_entered(participant)


func _on_participant_entered(participant: NetwParticipant) -> void:
	spawn_participant(participant)


## Server only. Spawns one player for [param participant].
func spawn_participant(participant: NetwParticipant) -> void:
	assert(multiplayer.is_server())
	if participant == null or participant.join == null:
		return
	if _has_player(participant.join):
		return

	var ordered := ctx.scene.participants
	ordered.sort_custom(
		func(a: NetwParticipant, b: NetwParticipant) -> bool:
			return a.peer_id < b.peer_id
	)
	var spawn_index := maxi(ordered.find(participant), 0)
	var data := {
		peer_id = participant.peer_id,
		spawn_index = spawn_index,
		username = participant.username,
	}
	NetwEntity.spawn_for(self, participant, data)


func _spawn_player(data: Dictionary) -> Node:
	var player := PLAYER_SCENE.instantiate()
	var peer_id := int(data.peer_id)
	var username := str(data.username)
	var spawn_index := int(data.spawn_index)

	var world := ctx.scene.level
	var score := world.get_node("Score")
	score.add_player(peer_id, username)

	player.position = _get_spawn_position(spawn_index)

	var label := player.get_node("%label") as Label
	label.text = username

	return player


func _has_player(rj: ResolvedJoin) -> bool:
	var players_root := get_node_or_null(spawn_path)
	return NetwEntity.find(players_root, rj) != null if players_root else false


func _get_spawn_position(spawn_index: int) -> Vector2:
	var world := ctx.scene.level

	var spawn_points := world.get_node("SpawnPoints")

	var point_index := spawn_index % spawn_points.get_child_count()
	var marker := spawn_points.get_child(point_index) as Node2D
	return marker.position if marker else Vector2.ZERO
