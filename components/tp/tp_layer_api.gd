@abstract
class_name TPLayerAPI
extends CanvasLayer

signal configured


@export var transition_progress: TextureProgressBar
@export var transition_anim: AnimationPlayer


func _init() -> void:
	configured.connect(_on_multiplayer_configured)

@abstract
func teleport_out() -> void

@abstract
func teleport_in() -> void


func _on_multiplayer_configured() -> void:
	if multiplayer.is_server():
		queue_free()
