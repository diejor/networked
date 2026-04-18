## Typed event for lobby lifecycle (spawned/despawned).
class_name NetLobbyEvent
extends RefCounted

var tree_name: String
var event: String
var lobby_name: String


func to_dict() -> Dictionary:
	return {
		"tree_name": tree_name,
		"event": event,
		"lobby_name": lobby_name,
	}
