extends MultiplayerSpawner

const BOMB = preload("uid://3uxvvsya0q1t")

func _init() -> void:
	spawn_function = _spawn_bomb


func _spawn_bomb(data: Array) -> Area2D:
	if data.size() != 2 or typeof(data[0]) != TYPE_VECTOR2 or typeof(data[1]) != TYPE_INT:
		return null

	var bomb: Area2D = BOMB.instantiate()
	bomb.position = data[0]
	bomb.from_player = data[1]
	return bomb
