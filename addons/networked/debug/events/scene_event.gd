## Typed wrapper for scene lifecycle events (spawned, despawned).
class_name NetSceneEvent
extends RefCounted

var tree_name: String
var scene_name: String
var event: String # "spawned" | "despawned"


## Serializes this event into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"tree_name": tree_name,
		"scene_name": scene_name,
		"event": event,
	}
