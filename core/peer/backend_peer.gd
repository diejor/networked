class_name BackendPeer
extends Resource

## Base class for network transports. 
## Extend this class to implement ENet, WebSocket, WebRTC, etc.

var api := SceneMultiplayer.new()

# Virtual method for creating a server
func host() -> Error:
	push_error("`%s` not implemented in base `BackendPeer`. \
Please use one implementation instead." % [host.get_method()])
	return ERR_UNAVAILABLE

# Virtual method for connecting as a client
func join(_server_address: String, _username: String = "") -> Error:
	push_error("`%s` not implemented in base `BackendPeer`. \
Please use one implementation instead." % [join.get_method()])
	return ERR_UNAVAILABLE

func configure_tree(tree: SceneTree, root_path: NodePath) -> void:
	api.root_path = root_path
	tree.set_multiplayer(api, root_path)

func poll(_dt: float) -> void:
	if api and api.has_multiplayer_peer():
		api.poll()

func peer_reset_state() -> void:
	if api.has_multiplayer_peer() and api.multiplayer_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED:
		api.multiplayer_peer.close()

	api.multiplayer_peer = OfflineMultiplayerPeer.new()
