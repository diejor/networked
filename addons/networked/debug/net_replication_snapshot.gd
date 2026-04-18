## Typed snapshot of node replication state.
class_name NetReplicationSnapshot
extends RefCounted

var tree_name: String
var node_path: String
var properties: Dictionary
var inventory: Array = []


func to_dict() -> Dictionary:
	var d := {
		"tree_name": tree_name,
		"node_path": node_path,
		"properties": properties,
	}
	if not inventory.is_empty():
		d["inventory"] = inventory
	return d
