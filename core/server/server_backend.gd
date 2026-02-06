extends Resource
class_name MultiplayerServerBackend

## Base class for server-side transports.

var multiplayer_api := SceneMultiplayer.new()
var multiplayer_peer: MultiplayerPeer

func create_server() -> Error:
	push_error(
		"Calling `create_server` directly on 
		`%s` is not allowed and should be implemented." % str(get_class()))
	return ERR_UNAVAILABLE

func configure_tree(tree: SceneTree, root_path: NodePath) -> void:
	multiplayer_api.multiplayer_peer = multiplayer_peer
	multiplayer_api.root_path = root_path
	tree.set_multiplayer(multiplayer_api, root_path)

func poll(_dt: float) -> void:
	if multiplayer_api and multiplayer_api.has_multiplayer_peer():
		multiplayer_api.poll()

func peer_reset_state() -> void:
	multiplayer_peer = null
	multiplayer_api.multiplayer_peer = null
