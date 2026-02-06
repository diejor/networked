extends SubViewport

func _ready() -> void:
	if not multiplayer.is_server():
		world_2d = get_tree().root.world_2d
