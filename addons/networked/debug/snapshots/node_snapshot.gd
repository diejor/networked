## Typed snapshot of a networked node's debug state at a point in time.
##
## Created via [method from_node] and attached to [NetTopologyManifest] so the
## editor panel can display synchronized property values alongside validation
## errors.
## [br][br]
## Production nodes may contribute additional state by implementing:
## [br]
## [codeblock]
## func _get_net_debug_state() -> Dictionary:
##     return { "health": health, "state": state_machine.current }
## [/codeblock]
## [br]
## The return value must contain only basic serializable types ([String], [int],
## [float], [bool], [Array], [Dictionary]). Object references are stripped with
## a warning.
class_name NetNodeSnapshot
extends RefCounted

var node_path: String
var node_name: String
var is_in_tree: bool
var authority: int

## Current values of all synchronized properties, keyed by [NodePath] string.
var sync_properties: Dictionary

## Extra state contributed by the node via [code]_get_net_debug_state()[/code].
var debug_state: Dictionary


## Builds a snapshot from [param node].
static func from_node(node: Node) -> NetNodeSnapshot:
	var snap := NetNodeSnapshot.new()
	snap.node_path = str(node.get_path()) if node.is_inside_tree() else ""
	snap.node_name = node.name
	snap.is_in_tree = node.is_inside_tree()
	snap.authority = node.get_multiplayer_authority()
	snap.sync_properties = _collect_sync_properties(node)

	if node.has_method("_get_net_debug_state"):
		var raw: Variant = node.call("_get_net_debug_state")
		if raw is Dictionary:
			snap.debug_state = _sanitize(raw as Dictionary)
		else:
			push_warning(
				"NetNodeSnapshot: %s._get_net_debug_state() must return " + \
				"Dictionary, got %s" % \
				[node.name, type_string(typeof(raw))]
			)

	return snap


## Serializes this snapshot into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"node_path": node_path,
		"node_name": node_name,
		"is_in_tree": is_in_tree,
		"authority": authority,
		"sync_properties": sync_properties,
		"debug_state": debug_state,
	}


static func _collect_sync_properties(node: Node) -> Dictionary:
	var props: Dictionary = {}
	for sync: MultiplayerSynchronizer in \
			SynchronizersCache.get_synchronizers(node):
		if not sync.replication_config:
			continue
		
		var root_node: Node = (
			sync.get_node(sync.root_path) if sync.root_path != NodePath(".") \
			else sync.get_parent()
		)
		if not is_instance_valid(root_node):
			continue
		
		for prop_path: NodePath in sync.replication_config.get_properties():
			var s := str(prop_path)
			var colon := s.rfind(":")
			if colon < 0:
				continue
			
			var node_part := s.substr(0, colon)
			var prop_name := s.substr(colon + 1)
			var target: Node = (
				root_node if node_part.is_empty() or node_part == "." \
				else root_node.get_node_or_null(node_part)
			)
			if is_instance_valid(target):
				var val: Variant = target.get(prop_name)
				if typeof(val) not in [
					TYPE_OBJECT,
					TYPE_RID,
					TYPE_CALLABLE,
					TYPE_SIGNAL
				]:
					props[s] = val
	return props


static func _sanitize(d: Dictionary) -> Dictionary:
	var out: Dictionary = {}
	for k: Variant in d:
		var v: Variant = d[k]
		if typeof(v) in [TYPE_OBJECT, TYPE_RID, TYPE_CALLABLE, TYPE_SIGNAL]:
			push_warning(
				"NetNodeSnapshot: _get_net_debug_state() key '%s' has " + \
				"non-serializable type %s — skipped" % \
				[str(k), type_string(typeof(v))]
			)
			continue
		out[k] = v
	return out
