## Adapter that converts a raw [NetDebugManifest] dictionary into structured view-model
## entries consumed by [PanelCrashManifest].
##
## All string-building and data-parsing lives here. The UI contains zero formatting logic.
@tool
class_name ManifestFormatter
extends RefCounted

## Translate a raw manifest [Dictionary] into a structured entry ready for 
## the [Tree] widget.
##
## Returned structure:
## [codeblock]
## {
##   label: String,          # top-level row text
##   trigger: String,
##   cid: String,
##   cid_timeline: Array[String],
##   frame: int,
##   timestamp_usec: int,
##   player_name: String,
##   in_tree: bool,
##   network_state: Dictionary,
##   error_text: String,
##   preflight: Array,        # formatted preflight rows  [{label, color, tooltip}]
##   telemetry: Array,        # formatted telemetry rows [{label, tooltip}]
## }
static func format(d: Dictionary, alias_map: Dictionary) -> Dictionary:
	var trigger: String = d.get("trigger", "UNKNOWN")
	var cid: String = d.get("cid", "?")
	var frame: int = d.get("frame", 0)
	var player: String = d.get("player_name", "?")

	var error_text: String = d.get("error_text", "")
	var raw_errors: Array = d.get("errors", [])
	if error_text.is_empty() and not raw_errors.is_empty():
		error_text = "\n".join(raw_errors)
	
	if error_text.is_empty():
		error_text = "[MISSING ERROR DATA - Check C++ watchdog or validation logic]"

	return {
		"label": "%s  @ frame %d" % [trigger, frame],
		"trigger": trigger,
		"cid": cid,
		"cid_timeline": d.get("cid_timeline", []),
		"frame": frame,
		"timestamp_usec": d.get("timestamp_usec", 0),
		"player_name": player,
		"in_tree": d.get("in_tree", false),
		"network_state": d.get("network_state", {}),
		"error_text": error_text,
		"active_scene": d.get("active_scene", ""),
		"preflight": _format_preflight(d.get("preflight_snapshot", []), alias_map),
		"telemetry": _format_telemetry(d.get("telemetry_slice", [])),
		"node_snapshot": d.get("node_snapshot", {}),
	}


static func _format_preflight(snapshot: Array, alias_map: Dictionary) -> Array:
	var out: Array = []
	for s: Dictionary in snapshot:
		if s.has("type"):
			var broadcast: bool = s.get("engine_broadcast", false)
			var raw_path: String = s.get("rel_path", s.get("path", "?"))
			var path_str: String = _alias_path(raw_path, alias_map)
			var auth_val: int = s.get("auth", 0)
			var auth_color: String = "green" if s.get("is_auth", false) else ("red" if auth_val == 0 else "yellow")
			var lobby_tag: String = (" lobby=%s" % s["lobby"]) if s.has("lobby") else ""
			out.append({
				"type": s.get("type", "?"),
				"path": path_str,
				"auth": auth_val,
				"auth_color": auth_color,
				"lobby": lobby_tag,
				"broadcast": broadcast,
				"label": "%s  %s" % [s.get("type", "?"), path_str],
				"tooltip": "auth=%d%s%s" % [auth_val, lobby_tag,
					" (engine broadcast)" if broadcast else ""],
			})
		else:
			# SaveSynchronizer audit entry
			var ok: bool = s.get("root_path_resolves", false)
			out.append({
				"type": "SaveAudit",
				"path": s.get("name", "?"),
				"auth": 0,
				"auth_color": "green" if ok else "red",
				"lobby": "",
				"broadcast": false,
				"label": "%s (Save Audit)" % s.get("name", "?"),
				"tooltip": "parent=%s  owner=%s  root_path=%s" % [
					s.get("parent", "?"), s.get("owner", "?"), s.get("root_path", "?")],
			})
	return out


static func _format_telemetry(slice: Array) -> Array:
	var out: Array = []
	for entry: Dictionary in slice:
		var frame: int = entry.get("frame", 0)
		var cid_stack: Array = entry.get("cid_stack", [])
		var peer_events: Array = entry.get("peer_events", [])
		var comp_events: Array = entry.get("component_events", [])

		# One row per frame that had meaningful events; skip empty frames.
		var events_text: PackedStringArray = []
		for pe: Dictionary in peer_events:
			events_text.append("peer_%d %s" % [pe.get("peer_id", 0), pe.get("event", "?")])
		for ce: Dictionary in comp_events:
			events_text.append(ce.get("event_type", "?"))

		if events_text.is_empty() and cid_stack.is_empty():
			continue

		var cid_short: String = cid_stack[0].substr(0, 16) if not cid_stack.is_empty() else ""
		out.append({
			"label": "f%d  %s" % [frame, "  ".join(events_text)] if not events_text.is_empty()
				else "f%d  [cid: %s]" % [frame, cid_short],
			"tooltip": "cid_stack: %s\npeer_events: %s\ncomp_events: %s" % [
				str(cid_stack), str(peer_events), str(comp_events)],
		})
	return out


## Substitutes known lobby level [param path] with a readable [param alias_map] 
## (e.g. [code]"/root/.../Level1" → "[Lobby:Level1]"[/code]).
static func _alias_path(path: String, alias_map: Dictionary) -> String:
	for prefix: String in alias_map:
		if path.begins_with(prefix):
			return path.replace(prefix, alias_map[prefix])
	return path
