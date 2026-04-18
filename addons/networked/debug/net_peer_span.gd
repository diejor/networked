## Peer-aware span for server-initiated multiplayer operations.
##
## Extends [NetSpan] with an explicit list of affected client peers, enabling
## the editor to correlate client-side C++ errors (caught by [ErrorWatchdog])
## with the server-side operation that caused them.
##
## Tag peers with [method affects] as the operation progresses:
## [codeblock]
## var span = NetTrace.begin_peer("lobby_spawn")
## span.affects(peer_id).step("visibility_set")
## set_visibility_for(peer_id, true)
## span.end()  # or span.fail("simplify_path_race")
## [/codeblock]
class_name NetPeerSpan
extends NetSpan

## Peers this operation is expected to affect. Populated via [method affects].
var affected_peers: Array[int] = []


func _init(p_id: StringName, p_label: String, meta: Dictionary = {}, p_tree_name: String = "", follows_from: CheckpointToken = null) -> void:
	super(p_id, p_label, meta, p_tree_name, follows_from)


## Tags [param peer_id] as a peer affected by this operation.
## Returns [code]self[/code] for method chaining.
func affects(peer_id: int) -> NetPeerSpan:
	if peer_id not in affected_peers:
		affected_peers.append(peer_id)
	return self


func _get_affected_peers() -> Array[int]:
	return affected_peers
