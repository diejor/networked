## Cached lookup helpers for [MultiplayerSynchronizer] nodes.
##
## Tree traversals are expensive; results are stored on the target node's metadata
## under [code]"cached_synchronizers"[/code] and reused until explicitly invalidated.
class_name SynchronizersCache
extends RefCounted

const META_KEY := &"cached_synchronizers"

## Returns all [MultiplayerSynchronizer] nodes whose [code]root_path[/code] points to [param target_node].
##
## Result is cached in metadata during gameplay.  The editor always performs a live search.
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
				if not is_instance_valid(sync) or sync.is_queued_for_deletion():
					is_cache_valid = false
					break
					
			if is_cache_valid:
				return cached
				
	synchronizers.assign(target_node.find_children("*", "MultiplayerSynchronizer"))
	
	var filtered_syncs := synchronizers.filter(func(sync: MultiplayerSynchronizer):
		return sync.root_path and sync.has_node(sync.root_path) and sync.get_node(sync.root_path) == target_node
	)
	
	if not Engine.is_editor_hint():
		target_node.set_meta(META_KEY, filtered_syncs)
		
	return filtered_syncs


## Returns only the [MultiplayerSynchronizer] nodes that are owned by [param target_node] in the scene tree.
static func get_client_synchronizers(target_node: Node) -> Array[MultiplayerSynchronizer]:
	return get_synchronizers(target_node).filter(func(sync: MultiplayerSynchronizer):
		return sync.owner == target_node
	)


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
