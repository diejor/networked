## Abstract base resource for network transports used by [MultiplayerTree].
##
## Subclass this to implement a new transport (ENet, WebSocket, WebRTC, etc.).
## Override [method host], [method join], and [method _get_backend_warnings].
@tool
@abstract
class_name BackendPeer
extends Resource

## The [SceneMultiplayer] instance owned by this backend.
var api := SceneMultiplayer.new()


## Starts listening as a server. Returns [code]OK[/code] on success.
@abstract
func host() -> Error


## Connects to [param _server_address] as a client. Returns [code]OK[/code] on success.
@abstract
func join(_server_address: String, _username: String = "") -> Error


## Returns editor configuration warnings specific to this backend for the given [param tree].
@abstract
func _get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray


## Registers [member api] with the [SceneTree] under [param root_path].
func configure_tree(tree: SceneTree, root_path: NodePath) -> void:
	api.root_path = root_path
	tree.set_multiplayer(api, root_path)


## Polls the underlying [MultiplayerPeer] each frame.
func poll(_dt: float) -> void:
	if api and api.has_multiplayer_peer():
		api.poll()


## Closes and clears the active [MultiplayerPeer], returning this backend to a disconnected state.
func peer_reset_state() -> void:
	if not api:
		return
	if api.has_multiplayer_peer():
		api.multiplayer_peer.close()
	api.multiplayer_peer = null


## Returns the address clients should use to join a hosted session.
##
## Override in subclasses that use dynamic addresses (e.g. room codes). Defaults to [code]"localhost"[/code].
func get_join_address() -> String:
	return "localhost"


## Called after this backend is duplicated by [MultiplayerTree]'s backend setter.
##
## Override to preserve shared references that [method Resource.duplicate] would reset
## to their default values.
func _copy_from(_source: BackendPeer) -> void:
	pass
