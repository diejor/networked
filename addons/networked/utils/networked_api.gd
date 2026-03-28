class_name NetworkedAPI
extends RefCounted

## Resolves the MultiplayerLobbyManager for the current multiplayer tree.
static func get_lobby_manager(node: Node) -> MultiplayerLobbyManager:
	var api := node.multiplayer as SceneMultiplayer
	if api and api.root_path:
		return node.get_node_or_null(api.root_path) as MultiplayerLobbyManager
	return null


## Safely retrieves the currently active Lobby. Returns null if there are zero or multiple active lobbies.
static func get_active_lobby(node: Node) -> Lobby:
	var manager := get_lobby_manager(node)
	if manager and manager.active_lobbies.size() == 1:
		return manager.active_lobbies.values()[0]
	return null


## Retrieves the global TPLayerAPI. Delegates to the lobby manager on clients.
static func get_tp_layer(node: Node) -> TPLayerAPI:
	if not node.is_inside_tree():
		return null
		
	if not node.multiplayer.is_server():
		var manager := get_lobby_manager(node)
		if manager:
			return manager.tp_layer
			
	return null
