## Per-scene visibility gate for one [MultiplayerScene] wrapper.
##
## Specialization of [InterestSynchronizer] that fixes
## [member InterestSynchronizer.anchor_strategy] to
## [code]ADMIT[/code] and exposes the legacy SS API used across the
## codebase: [method connect_peer] / [method disconnect_peer] for
## membership, [method track_node] / [method untrack_node] for
## per-entity gating, and [member connected_peers] / [member
## tracked_nodes] as observation surfaces.
##
## [b]Default-deny[/b] is what makes this safe for the scene case:
## the anchor is invisible to every peer until [method connect_peer]
## admits one, so [MultiplayerSpawner]s under the wrapper never
## replicate a partial subtree to outsider peers.
##
## [codeblock]
##     var scene := %SceneSynchronizer
##     scene.connect_peer(player.peer_id)
##     scene.track_node(player)
## [/codeblock]
class_name SceneSynchronizer
extends InterestSynchronizer


## Emitted when a tracked node enters the scene tree. Re-emits
## [signal InterestSynchronizer.entity_added] with the entity's owner.
signal spawned(node: Node)

## Emitted when a tracked node exits the scene tree.
signal despawned(node: Node)


## Alias for [member InterestSynchronizer.viewers]. Reads and writes
## the same dictionary. Kept for source compatibility with the legacy
## SS API.
var connected_peers: Dictionary[int, bool]:
	get:
		return viewers
	set(value):
		viewers = value


## Alias surfacing tracked nodes as [code]{Node: true}[/code] mirroring
## the legacy SS API. Derived from
## [member InterestSynchronizer.entities].
var tracked_nodes: Dictionary[Node, bool]:
	get:
		var out: Dictionary[Node, bool] = {}
		for entity: NetwEntity in entities:
			if is_instance_valid(entity) \
					and is_instance_valid(entity.owner):
				out[entity.owner] = true
		return out


func _init() -> void:
	anchor_strategy = InterestBinding.AnchorStrategy.ADMIT
	policy = Policy.HIDE_FROM_OUTSIDERS


func _ready() -> void:
	if layer_id.is_empty():
		layer_id = StringName("scene:%d" % get_instance_id())
	super._ready()
	name = "SceneSynchronizer"
	entity_added.connect(_emit_spawned_from_entity)
	entity_removed.connect(_emit_despawned_from_entity)


# ---------------------------------------------------------------------------
# Legacy SS API.
# ---------------------------------------------------------------------------

## Admits [param peer_id] to this scene. Equivalent to
## [method InterestSynchronizer.add_viewer] with a guard that rejects
## peer id [code]0[/code] (an invalid peer context).
func connect_peer(peer_id: int) -> void:
	if peer_id == 0:
		Netw.dbg.error(
				"SceneSynchronizer.connect_peer(0) is invalid.",
				[],
				func(m): push_error(m))
		return
	add_viewer(peer_id)


## Removes [param peer_id] from this scene. Equivalent to
## [method InterestSynchronizer.remove_viewer]; the deferred anchor
## hide and the entity-this-frame / anchor-next-frame ordering are
## handled by the binding.
func disconnect_peer(peer_id: int) -> void:
	remove_viewer(peer_id)


## Registers [param node]'s [NetwEntity] under this anchor. Resolves
## via [method NetwEntity.of]; nodes without a resolvable entity are
## logged and ignored.
##
## Also installs [method _on_spawned] / [method _on_despawned] on
## [param node]'s tree signals so [code]tp_component[/code]'s
## reparent flow can swap callbacks between the old and new scene's
## anchor when teleporting a player.
func track_node(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity == null:
		Netw.dbg.warn(
				"SceneSynchronizer.track_node: %s has no NetwEntity",
				[node.name])
		return
	add_entity(entity)
	var on_spawned := _on_spawned.bind(node)
	if not node.tree_entered.is_connected(on_spawned):
		node.tree_entered.connect(on_spawned)
	var on_despawned := _on_despawned.bind(node)
	if not node.tree_exiting.is_connected(on_despawned):
		node.tree_exiting.connect(on_despawned)


## Reverses [method track_node].
func untrack_node(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity == null:
		return
	var on_spawned := _on_spawned.bind(node)
	if node.tree_entered.is_connected(on_spawned):
		node.tree_entered.disconnect(on_spawned)
	var on_despawned := _on_despawned.bind(node)
	if node.tree_exiting.is_connected(on_despawned):
		node.tree_exiting.disconnect(on_despawned)
	remove_entity(entity)


## Deprecated: alias for [method track_node].
func track_player(player: Node) -> void:
	track_node(player)


## Deprecated: alias for [method untrack_node].
func untrack_player(player: Node) -> void:
	untrack_node(player)


## Forces a synchronous visibility pass. Wraps
## [method InterestSynchronizer.drive_now].
func update_players() -> void:
	drive_now()


## Calls [method MultiplayerSynchronizer.update_visibility] on every
## sync owned by [param node]. Kept for source compatibility; routine
## drive passes handle the same updates.
func update_player(node: Node) -> void:
	var entity := NetwEntity.of(node)
	if entity == null:
		return
	for sync in entity.synchronizers():
		if is_instance_valid(sync):
			sync.update_visibility()


## Legacy visibility filter callback. Returns the same verdict the
## anchor uses for [param peer_id].
func scene_visibility_filter(peer_id: int) -> bool:
	return _verdict_for(peer_id)


# ---------------------------------------------------------------------------
# Spawn/despawn signal bridge.
# ---------------------------------------------------------------------------

func _emit_spawned_from_entity(entity: NetwEntity) -> void:
	if not is_instance_valid(entity) \
			or not is_instance_valid(entity.owner):
		return
	if entity.owner.is_inside_tree():
		spawned.emit(entity.owner)
	else:
		entity.owner.tree_entered.connect(
				_emit_spawned_on_tree_entered.bind(entity.owner),
				CONNECT_ONE_SHOT)


func _emit_spawned_on_tree_entered(node: Node) -> void:
	if is_instance_valid(node):
		spawned.emit(node)


func _emit_despawned_from_entity(entity: NetwEntity) -> void:
	if is_instance_valid(entity) and is_instance_valid(entity.owner):
		despawned.emit(entity.owner)


# ---------------------------------------------------------------------------
# Spawn-signal wiring used by [method MultiplayerScene.hook_spawn_signals].
# ---------------------------------------------------------------------------

## Compatibility shim: [MultiplayerScene] connects each level
## [MultiplayerSpawner]'s [signal MultiplayerSpawner.spawned] to this
## method, and [method track_node] wires it to
## [signal Node.tree_entered] so reparented nodes re-enroll
## automatically. Idempotent via [method add_entity].
func _on_spawned(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity != null:
		add_entity(entity)


## Reverse of [method _on_spawned]. Removes the entity from the
## anchor but leaves [method track_node]'s tree signal connections in
## place so [code]tp_component[/code]'s teleport flow can still
## disconnect them after the reparent.
func _on_despawned(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity != null:
		remove_entity(entity)
