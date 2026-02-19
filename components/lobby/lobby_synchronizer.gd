class_name MultiplayerLobbySynchronizer
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

func update_client(client: Node) -> void:
	var state: StateSynchronizer = client.get_node("%StateSynchronizer")
	state.update()


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
	connect_client(node.get_multiplayer_authority())

func _on_despawned(node: Node) -> void:
	tracked_nodes.erase(node)
	disconnect_client(node.get_multiplayer_authority())
