## Abstract base class for client-side teleport transition overlays.
##
## Assign a concrete subclass instance to [member MultiplayerLobbyManager.tp_layer].
## Subclasses implement [method teleport_out] (fade/cover outgoing scene) and
## [method teleport_in] (reveal incoming scene). Both methods are awaitable.
@abstract
class_name TPLayerAPI
extends CanvasLayer

## Forwarded from [MultiplayerLobbyManager.configured]; used to free this node on the server.
signal configured

## Progress bar driven by the transition animation.
@export var transition_progress: TextureProgressBar
## [AnimationPlayer] that plays the teleport transition clip.
@export var transition_anim: AnimationPlayer


func _init() -> void:
	configured.connect(_on_multiplayer_configured)

## Plays the outgoing transition (cover the screen). Awaitable.
@abstract
func teleport_out() -> void

## Plays the incoming transition (reveal the screen). Awaitable.
@abstract
func teleport_in() -> void


func _on_multiplayer_configured() -> void:
	if multiplayer.is_server():
		queue_free()
