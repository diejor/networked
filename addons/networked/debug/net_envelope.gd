## Typed identity wrapper for all debug telemetry messages.
##
## Created once at the source; never mutated downstream. Replaces the
## [code]data.duplicate() + patched["tree_name"] = ...[/code] pattern.
## [br]
## Identity is defined by [member source_path] (unique within a process) and
## [member reporter_id] (differentiates two processes sharing one editor session).
class_name NetEnvelope
extends RefCounted


var source_path: String  ## str(mt.get_path()), e.g. "/root/Net/Server"
var reporter_id: String  ## 8-hex UUID; unique per reporter instance
var peer_id: int         ## multiplayer peer ID at emit time
var msg: StringName      ## full message name, e.g. "networked:clock_sample"
var payload: Dictionary  ## original data dict, never mutated
var frame: int           ## Engine.get_process_frames() at emit time


static func from_mt(mt: MultiplayerTree, p_msg: StringName, data: Dictionary, rid: String) -> NetEnvelope:
	var e := NetEnvelope.new()
	e.source_path = str(mt.get_path())
	e.reporter_id = rid
	e.peer_id = mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0
	e.msg = p_msg
	e.payload = data
	e.frame = Engine.get_process_frames()
	return e


## Last segment of source_path — used as the human-readable peer label in the UI.
func display_name() -> String:
	var np := NodePath(source_path)
	return np.get_name(np.get_name_count() - 1) if np.get_name_count() > 0 else source_path


## Unique key for the peer registry and adapter lookup.
## Combines source_path (unique per process) with reporter_id (unique per OS process).
func peer_key() -> String:
	return "%s|%s" % [source_path, reporter_id]


func to_dict() -> Dictionary:
	return {
		"source_path": source_path,
		"reporter_id": reporter_id,
		"peer_id": peer_id,
		"msg": String(msg),
		"payload": payload,
		"frame": frame,
	}


static func from_dict(d: Dictionary) -> NetEnvelope:
	var e := NetEnvelope.new()
	e.source_path = d.get("source_path", "")
	e.reporter_id = d.get("reporter_id", "")
	e.peer_id = d.get("peer_id", 0)
	e.msg = StringName(d.get("msg", ""))
	e.payload = d.get("payload", {})
	e.frame = d.get("frame", 0)
	return e
