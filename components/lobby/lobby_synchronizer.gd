class_name LobbySynchronizer
extends MultiplayerSynchronizer

@export var connected_clients: Dictionary[int, bool]:
	get:
		return connected_clients
	set(clients):
		connected_clients = clients
		update_clients.call_deferred()

var tracked_nodes: Dictionary[Node, bool]

func _ready() -> void:
	delta_synchronized.connect(update_clients)


func track_player(player: Node) -> void:
	player.tree_entered.connect(_on_spawned.bind(player))
	player.tree_exiting.connect(_on_despawned.bind(player))


func update_clients() -> void:
	for client: Node in tracked_nodes.keys():
		update_client(client)

func update_client(node: Node) -> void:
	var client := ClientComponent.unwrap(node)
	client.update_synchronizers()


# Very important the order in which the client visibility is handled:
# `https://github.com/godotengine/godot/issues/68508#issuecomment-2597110958`

func connect_client(peer_id: int) -> void:
	set_visibility_for(peer_id, true)
	connected_clients[peer_id] = true
	update_clients()


func disconnect_client(peer_id: int) -> void:
	connected_clients.erase(peer_id)
	update_clients()
	
	# trick to call last
	set_visibility_for.call_deferred(peer_id, false) 


func _on_spawned(node: Node) -> void:
	tracked_nodes[node] = true
	var syncs := get_synchronizers(node)
	for sync in syncs:
		sync.add_visibility_filter(scene_visibility_filter)
	connect_client(node.get_multiplayer_authority())

func _on_despawned(node: Node) -> void:
	tracked_nodes.erase(node)
	var syncs := get_synchronizers(node)
	for sync in syncs:
		sync.remove_visibility_filter(scene_visibility_filter)
	disconnect_client(node.get_multiplayer_authority())

func get_synchronizers(node: Node) -> Array[MultiplayerSynchronizer]:
	var synchronizers: Array[MultiplayerSynchronizer] = []
	synchronizers.assign(node.find_children("*", "MultiplayerSynchronizer"))
	return synchronizers

func scene_visibility_filter(peer_id: int) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	
	var res: bool = peer_id in connected_clients
	return res
