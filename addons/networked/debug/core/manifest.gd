## Base class for typed crash manifests crossing the [EngineDebugger] boundary.
##
## Holds the fields common to every manifest. Subclasses add trigger-specific
## fields and call [code]super.to_dict()[/code] when serializing.
## [br][br]
## [b]Key invariant:[/b] [method to_dict] must produce the exact key set that
## [code]ManifestFormatter.format[/code] expects. Do not rename keys without
## updating the formatter.
class_name NetManifest
extends RefCounted

var trigger: String
var cid: String
var cid_timeline: Array[String]
var frame: int
var timestamp_usec: int
var active_scene: String
var network_state: Dictionary
var telemetry_slice: Array
var node_snapshot: NetNodeSnapshot

## Unique ID for this specific emission, used for deduplication in the editor.
var uid: String = "%d_%d" % [Time.get_ticks_usec(), randi()]

## Weak reference to the [MultiplayerTree] that produced this manifest.
## Used for routing via [code]emit_debug_event[/code].
var _mt: WeakRef


## Returns [code]true[/code] if the manifest contains all required base fields.
## [br][br]
## Logs a warning to the console if the contract is violated.
func validate_contract() -> bool:
	var missing: Array[String] = []
	if trigger.is_empty():
		missing.append("trigger")
	if cid.is_empty():
		missing.append("cid")
	if uid.is_empty():
		missing.append("uid")
	
	if not missing.is_empty():
		push_warning(
			"NetManifest: Contract violation! Missing fields: %s" % \
			[str(missing)]
		)
		return false
	return true


## Serializes this manifest into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"uid": uid,
		"trigger": trigger,
		"cid": cid,
		"cid_timeline": cid_timeline,
		"frame": frame,
		"timestamp_usec": timestamp_usec,
		"active_scene": active_scene,
		"network_state": network_state,
		"telemetry_slice": telemetry_slice,
		"node_snapshot": node_snapshot.to_dict() if node_snapshot else {},
	}
