## Adapter that formats and buffers crash manifests for one peer.
##
## [member ring_buffer] entries are already-formatted view-model dictionaries
## as returned by [ManifestFormatter.format]. Formatting happens here so that
## [PanelCrashManifest] receives ready-to-render data with no transformation work.
##
## [member alias_map] is a shared reference owned by [DebuggerSession]; the adapter
## reads it at [method feed] time so aliases are always current.
@tool
class_name CrashAdapter
extends PanelDataAdapter

## Shared reference to [DebuggerSession]'s alias map (path prefix → readable alias).
## Do not replace this reference — mutate the original dict via the session.
var alias_map: Dictionary = {}


func _init(p_tree_name: String, p_alias_map: Dictionary) -> void:
	tree_name = p_tree_name
	panel_type = PanelType.CRASH
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[PanelType.CRASH]]
	alias_map = p_alias_map


## Formats [param d] via [ManifestFormatter] and appends the result to the buffer.
func feed(d: Dictionary) -> void:
	var entry: Dictionary = ManifestFormatter.format(d, alias_map)
	_push(entry)


## Returns the total number of recorded crashes, e.g. [code]"3 crashes"[/code].
func get_current_label() -> String:
	return "%d crash%s" % [ring_buffer.size(), "es" if ring_buffer.size() != 1 else ""]
