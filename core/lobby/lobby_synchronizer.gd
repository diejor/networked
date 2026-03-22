class_name LobbySynchronizer
extends MultiplayerSynchronizer

## Manages multiplayer visibility and synchronization state for a specific lobby.
##
## Tracks nodes (like players) that enter the lobby and dynamically updates 
## their visibility filters so that data is only synchronized to peers currently in the same lobby.

@export var connected_clients: Dictionary[int, bool]:
	get:
		return connected_clients
	set(clients):
		connected_clients = clients
		update_clients.call_deferred()

var tracked_nodes: Dictionary[Node, bool]


func _ready() -> void:
	delta_synchronized.connect(update_clients)


## Binds a player's lifecycle to the lobby's visibility filters.
func track_player(player: Node) -> void:
	player.tree_entered.connect(_on_spawned.bind(player))
	player.tree_exiting.connect(_on_despawned.bind(player))


## Forces a visibility update for all synchronizers belonging to tracked nodes in this lobby.
func update_clients() -> void:
	for client: Node in tracked_nodes.keys():
		update_client(client)


## Forces a visibility update for a specific node's cached synchronizers.
func update_client(node: Node) -> void:
	for sync in SynchronizersCache.get_synchronizers(node):
		sync.update_visibility()


## Registers a peer as connected to this lobby and updates visibility states.
func connect_client(peer_id: int) -> void:
	set_visibility_for(peer_id, true)
	connected_clients[peer_id] = true
	update_clients()


## Unregisters a peer from this lobby and safely detaches their visibility.
func disconnect_client(peer_id: int) -> void:
	connected_clients.erase(peer_id)
	update_clients()
	
	# Very important the order in which the client visibility is handled:
	# `https://github.com/godotengine/godot/issues/68508#issuecomment-2597110958`
	set_visibility_for.call_deferred(peer_id, false) 


func _on_spawned(node: Node) -> void:
	tracked_nodes[node] = true
	
	for sync in SynchronizersCache.get_synchronizers(node):
		sync.add_visibility_filter(scene_visibility_filter)
	
	connect_client(node.get_multiplayer_authority())


func _on_despawned(node: Node) -> void:
	tracked_nodes.erase(node)
	
	for sync in SynchronizersCache.get_synchronizers(node):
		sync.remove_visibility_filter(scene_visibility_filter)
	
	disconnect_client(node.get_multiplayer_authority())


## Determines if a specific peer is allowed to receive synchronization data for this lobby's nodes.
func scene_visibility_filter(peer_id: int) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	
	if peer_id == 0:
		return false
	
	var res: bool = peer_id in connected_clients
	return res
