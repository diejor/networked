extends Node

var finished: Array[int] = []


func _init() -> void:
	Netw.configure_entity(self)


@rpc("authority", "call_remote", "reliable")
func slow(frames: int, value: int) -> int:
	for frame in frames:
		await get_tree().process_frame
	finished.append(value)
	return value
