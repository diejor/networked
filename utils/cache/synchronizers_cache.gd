class_name SynchronizersCache
extends Object

const META_KEY := &"cached_synchronizers"

static func get_synchronizers(node: Node) -> Array[MultiplayerSynchronizer]:
	if node.has_meta(META_KEY):
		var cached: Array[MultiplayerSynchronizer] = []
		cached.assign(node.get_meta(META_KEY))
		return cached
	
	var synchronizers: Array[MultiplayerSynchronizer] = []
	synchronizers.assign(node.find_children("*", "MultiplayerSynchronizer"))
	
	node.set_meta(META_KEY, synchronizers)
	return synchronizers

static func remove(node: Node) -> void:
	if node.has_meta(META_KEY):
		node.remove_meta(META_KEY)
