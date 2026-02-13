class_name MultiplayerClientBackend
extends Resource


## Base class for client-side transports (WebSocket, WebRTC, Offline, etc.)

var api := SceneMultiplayer.new()

func create_connection(_server_address: String, _username: String) -> Error:
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
