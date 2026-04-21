## Base class for typed crash manifests crossing the [EngineDebugger] boundary.
##
## Holds the fields common to every manifest. Subclasses add trigger-specific
## fields and call [code]super.to_dict()[/code] when serializing.
##
## [b]Key invariant:[/b] [method to_dict] must produce the exact key set that
## [ManifestFormatter.format] expects. Do not rename keys without updating the
## formatter.
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

## Unique ID for this specific emission, used for deduplication in the editor.
var uid: String = "%d_%d" % [Time.get_ticks_usec(), randi()]

## Weak reference to the MultiplayerTree that produced this manifest.
## Used for routing via emit_debug_event.
var _mt: WeakRef


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
	}
