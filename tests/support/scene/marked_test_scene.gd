@tool
class_name MarkedTestScene
extends Node
## Test fixture: a scene root that marks itself as a multiplayer scene so the
## server-side load-and-verify path has a real on-disk mark to read.

static func _static_init() -> void:
	Netw.mark_multiplayer_scene(MarkedTestScene)


func _init() -> void:
	Netw.configure_multiplayer_scene(self)
