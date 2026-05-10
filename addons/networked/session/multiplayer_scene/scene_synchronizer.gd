## Manages per-scene synchronization visibility so each peer only receives data for their scene.
##
## Attach this synchronizer to a [Scene] node. Call [method track_node] for each entity node
## to register it; the synchronizer will then restrict replication so only peers inside this
## scene receive updates. Peer membership is tracked in [member connected_peers].
class_name SceneSynchronizer
extends MultiplayerSynchronizer

## Emitted when a tracked node enters the scene tree.
signal spawned(node: Node)
## Emitted when a tracked node exits the scene tree.
signal despawned(node: Node)

## Dictionary of peer IDs currently connected to this scene, mapped to [code]true[/code].
##
## Writing to this property defers a [method update_players] call.
@export var connected_peers: Dictionary[int, bool]:
	get:
		return connected_peers
	set(peers):
		connected_peers = peers
		update_players.call_deferred()

## Map of all nodes currently tracked by this synchronizer.
var tracked_nodes: Dictionary[Node, bool]


func _ready() -> void:
	name = "SceneSynchronizer"
	unique_name_in_owner = true
	public_visibility = false
	
	delta_synchronized.connect(update_players)
	
	if not owner:
		return
	
	root_path = get_path_to(owner)
	var config := SceneReplicationConfig.new()
	
	var path : =NodePath(str(owner.get_path_to(self)) + ":connected_peers")
	config.add_property(path)
	config.property_set_spawn(path, true)
	config.property_set_replication_mode(
		path, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE
	)
	
	replication_config = config


## Binds a node's lifecycle to the scene's visibility filters.
##
## When [param node] is already in the tree (e.g., a preplaced entity
## registering during its own [signal Node.tree_entered] handler), the
## spawn-side bookkeeping fires immediately so visibility filters are
## applied without waiting for the next tree-entry.
func track_node(node: Node) -> void:
	var on_spawned_bound := _on_spawned.bind(node)
	if not node.tree_entered.is_connected(on_spawned_bound):
		node.tree_entered.connect(on_spawned_bound)

	var on_despawned_bound := _on_despawned.bind(node)
	if not node.tree_exiting.is_connected(on_despawned_bound):
		node.tree_exiting.connect(on_despawned_bound)

	if node.is_inside_tree() and not tracked_nodes.has(node):
		_on_spawned(node)


## Removes a node from the scene's lifecycle tracking and visibility filters.
func untrack_node(node: Node) -> void:
	var on_spawned_bound := _on_spawned.bind(node)
	if node.tree_entered.is_connected(on_spawned_bound):
		node.tree_entered.disconnect(on_spawned_bound)

	var on_despawned_bound := _on_despawned.bind(node)
	if node.tree_exiting.is_connected(on_despawned_bound):
		node.tree_exiting.disconnect(on_despawned_bound)

	if node in tracked_nodes:
		_on_despawned(node)


## Deprecated: alias for [method track_node].
func track_player(player: Node) -> void:
	track_node(player)


## Deprecated: alias for [method untrack_node].
func untrack_player(player: Node) -> void:
	untrack_node(player)


## Forces a visibility update for all synchronizers belonging to tracked nodes in this scene.
func update_players() -> void:
	for node: Node in tracked_nodes.keys():
		update_player(node)


## Forces a visibility update for a specific node's cached synchronizers.
func update_player(node: Node) -> void:
	var syncs := SynchronizersCache.get_synchronizers(node)
	for sync in syncs:
		sync.update_visibility()


## Registers a peer as connected to this scene and updates visibility states.
func connect_peer(peer_id: int) -> void:
	Netw.dbg.debug("peer `peer_id=%s` connected to scene." % peer_id)
	set_visibility_for(peer_id, true)
	connected_peers[peer_id] = true
	update_players()


## Unregisters a peer from this scene and safely detaches their visibility.
##
## The deferred call order is intentional — see
## [code]https://github.com/godotengine/godot/issues/68508#issuecomment-2597110958[/code].
func disconnect_peer(peer_id: int) -> void:
	Netw.dbg.debug("peer `peer_id=%s` disconnected from scene." % peer_id)
	connected_peers.erase(peer_id)

	# Skip visibility updates for peers the engine has already purged.
	# `update_players()` would propagate filter results into
	# `_update_sync_visibility`, and `set_visibility_for` would propagate into
	# `_update_spawn_visibility` — both assert when `peers_info` no longer
	# contains the peer.
	if not _peer_is_live(peer_id):
		return

	update_players()
	# Very important the order in which the peer visibility is handled:
	# `https://github.com/godotengine/godot/issues/68508#issuecomment-2597110958`
	set_visibility_for.call_deferred(peer_id, false)


func _peer_is_live(peer_id: int) -> bool:
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return false
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	if peer_id == multiplayer.get_unique_id():
		return true
	return peer_id in multiplayer.get_peers()


func _on_spawned(node: Node) -> void:
	if tracked_nodes.has(node):
		return
	Netw.dbg.debug("%s spawned." % node.name)

	tracked_nodes[node] = true

	var syncs := SynchronizersCache.get_synchronizers(node)

	for sync in syncs:
		sync.add_visibility_filter(scene_visibility_filter)

	spawned.emit(node)


func _on_despawned(node: Node) -> void:
	if not tracked_nodes.has(node):
		return
	Netw.dbg.debug("%s despawned." % node.name)
	tracked_nodes.erase(node)

	for sync in SynchronizersCache.get_synchronizers(node):
		sync.remove_visibility_filter(scene_visibility_filter)

	despawned.emit(node)


## Visibility filter callback passed to each tracked node's synchronizers.
##
## Returns [code]true[/code] for the server and any peer present in [member connected_peers].
func scene_visibility_filter(peer_id: int) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	
	if peer_id == 0:
		return false
	
	var res: bool = peer_id in connected_peers
	return res
