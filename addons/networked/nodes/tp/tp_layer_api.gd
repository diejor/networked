## Abstract base class for client-side teleport transition overlays.
##
## Automatically registers itself with [NetwService] when added to the tree.
## Subclasses implement [method _teleport_out] (fade/cover outgoing scene) and
## [method _teleport_in] (reveal incoming scene). Both methods are awaitable.
@abstract
class_name TPLayerAPI
extends CanvasLayer

## Forwarded from [signal NetwSessionInterface.session_entered]. Frees this node
## on the server.
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

	NetwService.register(self, TPLayerAPI)
	var api := Netw.of(self)
	if api and not api.session.session_entered.is_connected(configured.emit):
		api.session.session_entered.connect(configured.emit)


func _ready() -> void:
	pass


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	var api := Netw.of(self)
	if api and api.session.session_entered.is_connected(configured.emit):
		api.session.session_entered.disconnect(configured.emit)

	NetwService.unregister(self, TPLayerAPI)


## Plays the outgoing transition (cover the screen). Awaitable.
@abstract
func _teleport_out() -> void


## Plays the incoming transition (reveal the screen). Awaitable.
@abstract
func _teleport_in() -> void


func _on_multiplayer_configured() -> void:
	# Dedicated servers have no viewport and never run client-side
	# presentation. Listen-server hosts also act as a local client and
	# must keep the layer alive to receive the teleport animation.
	var api := Netw.of(self)
	if api == null:
		return
	if api.role == NetwSessionInterface.Role.DEDICATED_SERVER:
		queue_free()
		return
	if not api.local_participant_joined.is_connected(_on_local_participant_joined):
		api.local_participant_joined.connect(_on_local_participant_joined)


# Plays the arrival animation when the local peer's player first appears.
# Presentation stays inside the presentation node.
func _on_local_participant_joined(_participant: NetwParticipant) -> void:
	_teleport_in()


## Returns the [MultiplayerTree] that owns this component's multiplayer session.
func get_multiplayer_tree() -> MultiplayerTree:
	return MultiplayerTree.for_node(self)
