class_name TPLayer
extends CanvasLayer

signal configured

@export var transition_progress: TextureProgressBar
@export var transition_anim: AnimationPlayer

var api: SceneMultiplayer:
	get:
		return multiplayer
var lobby_manager: MultiplayerLobbyManager:
	get:
		return get_node(api.root_path)

func _init() -> void:
	configured.connect(_on_multiplayer_configured)


func _ready() -> void:
	transition_progress.value = 0.0

func _on_multiplayer_configured() -> void:
	if multiplayer.is_server():
		queue_free()


func teleport_animation(animation: Callable) -> void:
	lobby_manager.process_mode = Node.PROCESS_MODE_DISABLED
	animation.call()
	await transition_anim.animation_finished
	lobby_manager.process_mode = Node.PROCESS_MODE_INHERIT

func teleport_in_animation() -> void:
	var anim: Callable = transition_anim.play_backwards.bind("tp")
	await teleport_animation(anim)

func teleport_out_animation() -> void:
	var anim: Callable = transition_anim.play.bind("tp")
	await teleport_animation(anim)
