## Typed identity wrapper for all debug telemetry messages.
##
## Created once at the source; never mutated downstream. Replaces the
## [code]data.duplicate() + patched["tree_name"] = ...[/code] pattern.
## [br][br]
## Identity is defined by [member source_path] (unique within a process) and
## [member reporter_id] (differentiates two processes sharing one editor
## session).
class_name NetEnvelope
extends RefCounted


## [code]str(mt.get_path())[/code], e.g. [code]"/root/Net/Server"[/code].
var source_path: String

## 8-hex UUID; unique per reporter instance.
var reporter_id: String

## Multiplayer peer ID at emit time.
var peer_id: int

## Full message name, e.g. [code]"networked:clock_sample"[/code].
var msg: StringName

## Original data dict, never mutated.
var payload: Dictionary

## [method Engine.get_process_frames] at emit time.
var frame: int


## Creates a [NetEnvelope] from a [MultiplayerTree] and message data.
static func from_mt(
	mt: MultiplayerTree,
	p_msg: StringName,
	data: Dictionary,
	rid: String
) -> NetEnvelope:
	var e := NetEnvelope.new()
	e.source_path = str(mt.get_path())
	e.reporter_id = rid
	e.peer_id = mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0
	e.msg = p_msg
	e.payload = data
	e.frame = Engine.get_process_frames()
	return e


## Returns the last segment of [member source_path] as a human-readable label.
func display_name() -> String:
	var np := NodePath(source_path)
	if np.get_name_count() > 0:
		return np.get_name(np.get_name_count() - 1)
	return source_path


## Returns a unique key combining [member source_path] and [member reporter_id].
func peer_key() -> String:
	return "%s|%s" % [source_path, reporter_id]


## Serializes this envelope into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"source_path": source_path,
		"reporter_id": reporter_id,
		"peer_id": peer_id,
		"msg": String(msg),
		"payload": payload,
		"frame": frame,
	}


## Creates a [NetEnvelope] from a [Dictionary].
static func from_dict(d: Dictionary) -> NetEnvelope:
	var e := NetEnvelope.new()
	e.source_path = d.get("source_path", "")
	e.reporter_id = d.get("reporter_id", "")
	e.peer_id = d.get("peer_id", 0)
	e.msg = StringName(d.get("msg", ""))
	e.payload = d.get("payload", {})
	e.frame = d.get("frame", 0)
	return e
