## Typed manifest for [code]TOPOLOGY_VALIDATION_FAILED[/code] events.
class_name NetTopologyManifest
extends NetManifest

var errors: Array[String]
var player_name: String
var in_tree: bool
## Optional structured snapshot of the player node at failure time.
var node_snapshot: NetNodeSnapshot


func to_dict() -> Dictionary:
	var d := super.to_dict()
	d["errors"] = errors
	d["player_name"] = player_name
	d["in_tree"] = in_tree
	d["node_snapshot"] = node_snapshot.to_dict() if node_snapshot else {}
	return d
