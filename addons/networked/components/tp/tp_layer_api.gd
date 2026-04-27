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

var _dbg: NetwHandle = Netw.dbg.handle(self)


func _init() -> void:
	configured.connect(_on_multiplayer_configured)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	var mt: MultiplayerTree = get_parent().get_parent() as MultiplayerTree
	assert(is_instance_valid(mt), "TPLayer must be added as a descendant of a MultiplayerTree")
	mt.register_service(self, TPLayerAPI)


func _ready() -> void:
	pass


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	var mt: MultiplayerTree = get_multiplayer_tree()
	if is_instance_valid(mt):
		mt.unregister_service(self, TPLayerAPI)

## Plays the outgoing transition (cover the screen). Awaitable.
@abstract
func teleport_out() -> void

## Plays the incoming transition (reveal the screen). Awaitable.
@abstract
func teleport_in() -> void


func _on_multiplayer_configured() -> void:
	if multiplayer.is_server():
		queue_free()


## Returns the [MultiplayerTree] that owns this component's multiplayer session.
func get_multiplayer_tree() -> MultiplayerTree:
	var api := multiplayer as SceneMultiplayer
	if not api:
		return null
	return api.get_meta(&"_multiplayer_tree", null) as MultiplayerTree
