## Typed wrapper for peer connection events.
class_name NetPeerEvent
extends RefCounted

var tree_name: String
var peer_id: int


## Serializes this event into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"tree_name": tree_name,
		"peer_id": peer_id,
	}
