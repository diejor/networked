## Manages per-scene synchronization visibility so each peer only receives
## data for their scene.
##
## Attach this synchronizer to a [Scene] node. Call [method track_node] for
## each entity node to register it; the synchronizer registers the entity
## as a subject of its [code]ISOLATE[/code] [NetwInterestLayer] so only
## peers added via [method connect_peer] receive updates.
##
## The layer's id is [code]scene:&lt;owner.name&gt;[/code]; downstream code can
## look it up via [code]ctx.interest.layer(&"scene:&lt;owner.name&gt;")[/code].
class_name SceneSynchronizer
extends MultiplayerSynchronizer

## Emitted when a tracked node enters the scene tree.
signal spawned(node: Node)
## Emitted when a tracked node exits the scene tree.
signal despawned(node: Node)

## Dictionary of peer IDs currently connected to this scene, mapped to [code]true[/code].
##
## Mirrors layer membership for replication to clients; layer.add_member /
## remove_member remain authoritative on the server.
@export var connected_peers: Dictionary[int, bool]

## Map of all nodes currently tracked by this synchronizer.
var tracked_nodes: Dictionary[Node, bool]

## The [NetwInterestLayer] owned by this synchronizer. Created lazily on
## first call to [method _ensure_layer] (server-side; clients leave it null).
var layer: NetwInterestLayer


func _ready() -> void:
	name = "SceneSynchronizer"
	unique_name_in_owner = true
	public_visibility = false

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

	if multiplayer.is_server():
		_ensure_layer()


func _exit_tree() -> void:
	if layer and not layer.is_disposed():
		layer.dispose_immediate()
	layer = null


## Binds a node's lifecycle to the scene's interest layer.
##
## When [param node] is already in the tree (e.g., a preplaced entity
## registering during its own [signal Node.tree_entered] handler), the
## spawn-side bookkeeping fires immediately.
func track_node(node: Node) -> void:
	var on_spawned_bound := _on_spawned.bind(node)
	if not node.tree_entered.is_connected(on_spawned_bound):
		node.tree_entered.connect(on_spawned_bound)

	var on_despawned_bound := _on_despawned.bind(node)
	if not node.tree_exiting.is_connected(on_despawned_bound):
		node.tree_exiting.connect(on_despawned_bound)

	if node.is_inside_tree() and not tracked_nodes.has(node):
		_on_spawned(node)


## Removes a node from the scene's lifecycle tracking and interest layer.
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


## Registers a peer as connected to this scene and adds them to the layer.
func connect_peer(peer_id: int) -> void:
	if peer_id == 0:
		Netw.dbg.error(
			"SceneSynchronizer.connect_peer(0) is invalid.",
			[],
			func(m): push_error(m)
		)
		return
	Netw.dbg.debug("peer `peer_id=%s` connected to scene." % peer_id)
	set_visibility_for(peer_id, true)
	connected_peers[peer_id] = true
	notify_property_list_changed()
	_ensure_layer()
	if layer:
		layer.add_member(peer_id)


## Unregisters a peer from this scene and safely detaches their visibility.
##
## See [code]https://github.com/godotengine/godot/issues/68508#issuecomment-2597110958[/code]
## for the deferred order.
func disconnect_peer(peer_id: int) -> void:
	Netw.dbg.debug("peer `peer_id=%s` disconnected from scene." % peer_id)
	connected_peers.erase(peer_id)
	notify_property_list_changed()
	if layer:
		layer.remove_member(peer_id)

	if not _peer_is_live(peer_id):
		return

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

	_ensure_layer()
	if layer:
		var entity := NetwEntity.of(node)
		if entity:
			layer.add_subject(entity)

	spawned.emit(node)


func _on_despawned(node: Node) -> void:
	if not tracked_nodes.has(node):
		return
	Netw.dbg.debug("%s despawned." % node.name)
	tracked_nodes.erase(node)

	if layer:
		var entity := NetwEntity.of(node)
		if entity:
			layer.remove_subject(entity)

	despawned.emit(node)


# Resolves the scene's layer. On the server this lazily creates an
# ISOLATE layer; on clients it picks up the mirror layer pushed by
# InterestService once the peer joins the scene.
func _ensure_layer() -> void:
	if is_instance_valid(layer) or not is_inside_tree():
		return
	var mt := MultiplayerTree.resolve(self)
	if not mt or not mt.interest:
		return
	var id := _layer_id()
	var existing := mt.interest.layer(id)
	if existing:
		layer = existing
		return
	if multiplayer != null and multiplayer.is_server():
		layer = mt.interest.create_layer(id, NetwInterestLayer.Policy.ISOLATE)


## Back-compat query: returns whether [param peer_id] would receive
## tracked-node updates under the current scene membership. Equivalent to
## [code]layer.has_member(peer_id)[/code] but resolves from
## [member connected_peers] so the answer is consistent on clients (which
## don't own the layer) and on freshly-constructed synchronizers (where
## the layer has not been created yet).
func scene_visibility_filter(peer_id: int) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	if peer_id == 0:
		return false
	return peer_id in connected_peers


func _layer_id() -> StringName:
	if not is_instance_valid(owner):
		return &"scene:<orphan:%d>" % get_instance_id()
	return StringName("scene:%s" % owner.name)
