## Typed event for peer connection and disconnection.
class_name NetPeerEvent
extends RefCounted

var tree_name: String
var peer_id: int


func to_dict() -> Dictionary:
	return {
		"tree_name": tree_name,
		"peer_id": peer_id,
	}
