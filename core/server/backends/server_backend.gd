extends Resource
class_name MultiplayerServerBackend

## Base class for server-side transports.

signal tree_configured()

var api := SceneMultiplayer.new()

func create_server() -> Error:
	push_error(
		"Calling `create_server` directly on 
		`%s` is not allowed and should be implemented." % str(get_class()))
	return ERR_UNAVAILABLE

func configure_tree(tree: SceneTree, root_path: NodePath) -> void:
	api.root_path = root_path
	tree.set_multiplayer(api, root_path)

func poll(_dt: float) -> void:
	assert(api)
	assert(api.has_multiplayer_peer())
	
	api.poll()

func peer_reset_state() -> void:
	assert(api.has_multiplayer_peer())
	
	if api.multiplayer_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED:
		api.multiplayer_peer.close()

	api.multiplayer_peer = OfflineMultiplayerPeer.new()
