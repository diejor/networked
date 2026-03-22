@tool
@abstract
class_name BackendPeer
extends Resource

## Base class for network transports. 
## Extend this class to implement ENet, WebSocket, WebRTC, etc.

var api := SceneMultiplayer.new()

@abstract
func host() -> Error

@abstract
func join(_server_address: String, _username: String = "") -> Error

@abstract
func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray

func configure_tree(tree: SceneTree, root_path: NodePath) -> void:
	api.root_path = root_path
	tree.set_multiplayer(api, root_path)

func poll(_dt: float) -> void:
	if api and api.has_multiplayer_peer():
		api.poll()

func peer_reset_state() -> void:
	if not api:
		return
	
	if api.has_multiplayer_peer():
		api.multiplayer_peer.close()
	
	api.multiplayer_peer = null

func get_join_address() -> String:
	return "localhost"
