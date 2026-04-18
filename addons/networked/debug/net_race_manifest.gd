## Typed manifest for [code]SERVER_SIMPLIFY_PATH_RACE[/code] events.
class_name NetRaceManifest
extends NetManifest

## Human-readable description of each detected race (used in fallback push_error).
var errors: Array[String]
## Raw race detail dicts from [NetRaceDetector] — consumed by [ManifestFormatter]
## for the Preflight Snapshot section of the editor panel.
var preflight_snapshot: Array
var player_name: String
var in_tree: bool


func to_dict() -> Dictionary:
	var d := super.to_dict()
	d["errors"] = errors
	d["preflight_snapshot"] = preflight_snapshot
	d["player_name"] = player_name
	d["in_tree"] = in_tree
	return d
