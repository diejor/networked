## Manages per-lobby synchronization visibility so each peer only receives data for their lobby.
##
## Attach this synchronizer to a [Lobby] node. Call [method track_player] for each player node
## to register it; the synchronizer will then restrict replication so only peers inside this
## lobby receive updates. Peer membership is tracked in [member connected_clients].
class_name LobbySynchronizer
extends MultiplayerSynchronizer

## Emitted when a tracked node enters the lobby scene tree.
signal spawned(client: Node)
## Emitted when a tracked node exits the lobby scene tree.
signal despawned(client: Node)

## Dictionary of peer IDs currently connected to this lobby, mapped to [code]true[/code].
##
## Writing to this property defers a [method update_clients] call.
@export var connected_clients: Dictionary[int, bool]:
	get:
		return connected_clients
	set(clients):
		connected_clients = clients
		update_clients.call_deferred()

## Map of all nodes currently tracked by this synchronizer.
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
##
## The deferred call order is intentional — see
## [code]https://github.com/godotengine/godot/issues/68508#issuecomment-2597110958[/code].
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
	spawned.emit(node)


func _on_despawned(node: Node) -> void:
	tracked_nodes.erase(node)
	
	for sync in SynchronizersCache.get_synchronizers(node):
		sync.remove_visibility_filter(scene_visibility_filter)
	
	disconnect_client(node.get_multiplayer_authority())
	despawned.emit(node)


## Visibility filter callback passed to each tracked node's synchronizers.
##
## Returns [code]true[/code] for the server and any peer present in [member connected_clients].
func scene_visibility_filter(peer_id: int) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	
	if peer_id == 0:
		return false
	
	var res: bool = peer_id in connected_clients
	return res
