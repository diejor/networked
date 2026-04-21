## Typed manifest for [code]TOPOLOGY_VALIDATION_FAILED[/code] events.
class_name NetTopologyManifest
extends NetManifest

var errors: Array[String]
var player_name: String
var in_tree: bool


func to_dict() -> Dictionary:
	var d := super.to_dict()
	d["errors"] = errors
	d["player_name"] = player_name
	d["in_tree"] = in_tree
	return d
