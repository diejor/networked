## Peer-aware extension of [NetSpan] for multi-client operations.
##
## Tracks which peer IDs are affected by a specific operation (e.g., a scene
## spawn intended for peers 2 and 3).
class_name NetPeerSpan
extends NetSpan

var _peers: Array[int] = []


## Records that this span affects the specified [param peer_id].
func affects(peer_id: int) -> void:
	if peer_id not in _peers:
		_peers.append(peer_id)


func _get_affected_peers() -> Array[int]:
	return _peers
