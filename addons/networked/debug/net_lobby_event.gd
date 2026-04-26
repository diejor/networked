## Typed wrapper for lobby lifecycle events (spawned, despawned).
class_name NetLobbyEvent
extends RefCounted

var tree_name: String
var lobby_name: String
var event: String # "spawned" | "despawned"


## Serializes this event into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"tree_name": tree_name,
		"lobby_name": lobby_name,
		"event": event,
	}
