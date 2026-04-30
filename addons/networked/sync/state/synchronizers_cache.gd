## Cached lookup helpers for [MultiplayerSynchronizer] nodes.
##
## Tree traversals are expensive; results are stored on the target node's metadata
## under [code]"cached_synchronizers"[/code] and reused until explicitly invalidated.
class_name SynchronizersCache
extends RefCounted

const META_KEY := &"cached_synchronizers"


## Returns all [MultiplayerSynchronizer] nodes whose [code]root_path[/code] points to [param target_node].
static func get_synchronizers(target_node: Node) -> Array[MultiplayerSynchronizer]:
	var synchronizers: Array[MultiplayerSynchronizer] = []
	if not target_node:
		return synchronizers
		
	if not Engine.is_editor_hint():
		if target_node.has_meta(META_KEY):
			var cached: Array[MultiplayerSynchronizer] = []
			cached.assign(target_node.get_meta(META_KEY))
			
			var is_cache_valid := true
			for sync in cached:
				if not is_instance_valid(sync) or sync.is_queued_for_deletion() \
					or not sync.is_inside_tree() \
					or not sync.has_node(sync.root_path) \
					or sync.get_node(sync.root_path) != target_node:
					is_cache_valid = false
					break
					
			if is_cache_valid:
				return cached
				
	synchronizers.assign(target_node.find_children("*", "MultiplayerSynchronizer"))

	var filtered_syncs: Array[MultiplayerSynchronizer] = []
	filtered_syncs.assign(synchronizers.filter(func(sync: MultiplayerSynchronizer):
		return sync.root_path and sync.has_node(sync.root_path) \
			and sync.get_node(sync.root_path) == target_node
	))

	var result: Array[MultiplayerSynchronizer] = []
	result.assign(filtered_syncs)

	if target_node.is_inside_tree():
		if not Engine.is_editor_hint():
			target_node.set_meta(META_KEY, result)
			_connect_invalidation(target_node)
	elif not Engine.is_editor_hint():
		var type_names := result.map(func(s: MultiplayerSynchronizer) -> String: return s.name)
		Netw.dbg.debug("SynchronizersCache: '%s' is off-tree; cache not written. Synchronizers found: [%s]" % [target_node.name, ", ".join(type_names)])
	
	return result


## Returns only the [MultiplayerSynchronizer] nodes that are owned by [param target_node] in the scene tree.
static func get_client_synchronizers(target_node: Node) -> Array[MultiplayerSynchronizer]:
	return get_synchronizers(target_node).filter(func(sync: MultiplayerSynchronizer):
		# In editor, owner is the scene root. In game, it might be different but 
		# for client-owned nodes it should match.
		return sync.owner == target_node or (Engine.is_editor_hint() and sync.owner == target_node.owner)
	)


## Returns a dictionary of all property paths tracked by client-owned synchronizers on [param target_node].
## [br]Key is the cleaned [StringName] of the path, value is the raw [NodePath].
static func get_all_synchronized_properties(target_node: Node) -> Dictionary[StringName, NodePath]:
	var result: Dictionary[StringName, NodePath] = {}
	if not target_node:
		return result
		
	for sync in get_client_synchronizers(target_node):
		if not sync.replication_config:
			continue
			
		for path in sync.replication_config.get_properties():
			var clean_name := _extract_clean_name(path)
			if clean_name != &"":
				result[clean_name] = path
				
	return result


## Safely resolves a value from a [param target] node using a [param path].
## Handles edge cases where [method Object.get_indexed] might fail in the editor.
static func resolve_value(target: Node, path: NodePath) -> Variant:
	if not target or path.is_empty():
		return null
		
	var res := target.get_node_and_resource(path)
	var obj: Object = res[0]
	var remaining_path: NodePath = res[2]
	
	if not obj or remaining_path.is_empty():
		return null
		
	return obj.get_indexed(remaining_path)


## Safely assigns a [param value] to a [param target] node using a [param path].
static func assign_value(target: Node, path: NodePath, value: Variant) -> void:
	if not target or path.is_empty():
		return
		
	var res := target.get_node_and_resource(path)
	var obj: Object = res[0]
	var remaining_path: NodePath = res[2]
	
	if obj and not remaining_path.is_empty():
		obj.set_indexed(remaining_path, value)


static func _extract_clean_name(path: NodePath) -> StringName:
	if path.get_subname_count() == 0:
		return &""
		
	var s := str(path)
	if s.begins_with(".:"):
		return StringName(s.trim_prefix(".:"))
	if s.begins_with(":"):
		return StringName(s.trim_prefix(":"))
	return StringName(s)


## Restricts all synchronizers on [param target_node] to only send data to the server peer.
static func sync_only_server(target_node: Node) -> void:
	for sync in get_synchronizers(target_node):
		sync.set_visibility_for(0, false)
		sync.set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)
		sync.update_visibility()


## Removes the cached synchronizer list from [param target_node]'s metadata.
static func clear_cache(target_node: Node) -> void:
	if target_node and target_node.has_meta(META_KEY):
		target_node.remove_meta(META_KEY)


## Connects [signal Node.child_entered_tree] to [method clear_cache] on [param node]
## exactly once, so that adding a new [MultiplayerSynchronizer] child auto-invalidates
## the cache.
static func _connect_invalidation(node: Node) -> void:
	const CONNECTED_META := &"_sc_invalidation_connected"
	if node.has_meta(CONNECTED_META):
		return
	node.set_meta(CONNECTED_META, true)
	node.child_entered_tree.connect(func(child: Node) -> void:
		if child is MultiplayerSynchronizer:
			clear_cache(node)
	)
