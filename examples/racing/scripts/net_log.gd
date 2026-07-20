class_name RacingNetLog extends Node
## Opt-in CSV recorder for one car's clock and reconciliation cadence.
##
## Rubber banding is a timing artifact, so it is only diagnosable against a
## timeline: which physics frames ran no tick or two, which server ticks consumed
## no input, and which of those line up with a correction. This records exactly
## that, one file per instance, and stays inert unless the [code]NETW_NETLOG[/code]
## environment variable is set, so a normal run pays nothing.
##
## [codeblock]
## # two instances, then correlate the two files offline
## NETW_NETLOG=1 godot --path . examples/racing/main.tscn
##
## user://netlog_client_7.csv
## kind,wall_ms,tick,ticks_this_frame,ack_age,consumed,missing,starved,held,drained,corrections
## FRAME,10233,612,1,1,1,0,0,0,0,0
## kind,wall_ms,tick,field,magnitude,teleported
## CORR,10250,614,sphere_position,0.371,false
## [/codeblock]
##
## Counter columns are per-frame deltas, not totals, so a row reads as "what
## happened in this frame". The counters come off
## [NetwLagCompensationInterface.PredictionHandle]:
## [member NetwLagCompensationInterface.PredictionHandle.starved_count] and
## [member NetwLagCompensationInterface.PredictionHandle.held_count] separate a
## server tick that had no input from one deliberately rebuilding the de-jitter
## depth, and
## [member NetwLagCompensationInterface.PredictionHandle.drained_count] shows the
## backlog being shed. A [code]ticks_this_frame[/code] of [code]0[/code] or
## [code]2[/code] is the local clock stretching to meet its calibration target.

## Environment variable that arms the recorder. Any non-empty value enables it.
const ENABLE_VAR := "NETW_NETLOG"

const FRAME_HEADER := "kind,wall_ms,tick,ticks_this_frame,ack_age,consumed," \
		+ "missing,starved,held,drained,resync,skipped,corrections,contacts,grounded"
const CORRECTION_HEADER := "kind,wall_ms,tick,field,magnitude,teleported"
const EVAL_HEADER := "kind,wall_ms,tick,recv_tick,ack,divergence,corrected"
const CONTACT_HEADER := "kind,wall_ms,tick"
const FIELD_HEADER := "kind,wall_ms,tick,field,error"

var _clock: NetwClockInterface
var _handle: NetwLagCompensationInterface.PredictionHandle
var _body: Node
var _file: FileAccess
var _ticks_this_frame: int = 0
var _contacts_this_frame: int = 0
var _previous: Dictionary = { }


## Returns [code]true[/code] when [constant ENABLE_VAR] arms the recorder, so a
## caller can skip composing one at all on a normal run.
static func armed() -> bool:
	return not OS.get_environment(ENABLE_VAR).is_empty()


## Starts recording [param entity]'s cadence against [param clock], opening
## [code]user://netlog_<role>_<id>.csv[/code]. A no-op when the recorder is not
## armed or the entity carries no prediction handle, so the caller never has to
## guard the call.
func start(entity: NetwEntity, clock: NetwClockInterface) -> void:
	if not armed() or not entity or not clock:
		return
	_handle = entity.prediction
	if not _handle:
		return
	_clock = clock
	_body = entity.owner

	# The name has to separate every recorder that can run at once. Two instances
	# on one machine share a user:// directory (hence the local peer id), and one
	# instance holds a recorder per car, whose entity ids collide whenever two
	# players share a username (hence the controlling peer, which never does).
	var role := "server" if multiplayer.is_server() else "client"
	var path := "user://netlog_%s_%d_car%d_%s.csv" % [
		role,
		multiplayer.get_unique_id(),
		entity.controller,
		String(entity.entity_id).validate_filename(),
	]
	_file = FileAccess.open(path, FileAccess.WRITE)
	if not _file:
		push_warning("net_log could not open %s" % path)
		return
	_file.store_line(FRAME_HEADER)
	_file.store_line(CORRECTION_HEADER)
	_file.store_line(EVAL_HEADER)
	_file.store_line(CONTACT_HEADER)
	_file.store_line(FIELD_HEADER)
	print("net_log recording to %s" % ProjectSettings.globalize_path(path))

	_snapshot_counters()
	_clock.on_tick.connect(_on_tick)
	_clock.after_tick_loop.connect(_on_after_tick_loop)
	_handle.pose_corrected.connect(_on_pose_corrected)
	_handle.state_evaluated.connect(_on_state_evaluated)


func _exit_tree() -> void:
	if _file:
		_file.close()
		_file = null


func _on_tick(_delta: float, _tick: int) -> void:
	_ticks_this_frame += 1


# One row per physics frame, whether or not it ran a tick. A frame that ran none
# is the interesting one, so the row is written unconditionally.
func _on_after_tick_loop() -> void:
	if not _file:
		return
	var deltas := _counter_deltas()
	_file.store_line("FRAME,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d" % [
		Time.get_ticks_msec(),
		_clock.tick,
		_ticks_this_frame,
		_handle.ack_age_ticks,
		deltas["consumed"],
		deltas["missing"],
		deltas["starved"],
		deltas["held"],
		deltas["drained"],
		deltas["resync"],
		deltas["skipped"],
		deltas["corrections"],
		_contacts_this_frame,
		1 if _grounded() else 0,
	])
	_ticks_this_frame = 0
	_contacts_this_frame = 0


# One row per corrected field, so a magnitude can be read against the epsilon
# that triggered it without parsing a nested payload.
func _on_pose_corrected(deltas: Dictionary, teleported: bool) -> void:
	if not _file:
		return
	for field: StringName in deltas:
		_file.store_line("CORR,%d,%d,%s,%.4f,%s" % [
			Time.get_ticks_msec(),
			_clock.tick,
			field,
			_magnitude(deltas[field]),
			"true" if teleported else "false",
		])


## Records a contact at the current tick. Call it wherever the game notifies the
## predictor through
## [method NetwLagCompensationInterface.PredictionHandle.notify_contact], so the
## log carries the collisions that open a correction-pause window. A no-op when
## the recorder is not running.
func mark_contact() -> void:
	_contacts_this_frame += 1
	if not _file:
		return
	_file.store_line("CONTACT,%d,%d" % [Time.get_ticks_msec(), _clock.tick])


# One row per authoritative state receive, carrying the divergence the predictor
# measured before its epsilon test. Corrections alone cannot show a divergence
# that grows and is never acted on, which is exactly what a paused correction
# window produces.
func _on_state_evaluated(
		recv_tick: int,
		ack: int,
		divergence: float,
		corrected: bool,
) -> void:
	if not _file:
		return
	var now := Time.get_ticks_msec()
	_file.store_line("EVAL,%d,%d,%d,%d,%.4f,%s" % [
		now,
		_clock.tick,
		recv_tick,
		ack,
		divergence,
		"true" if corrected else "false",
	])
	# The scalar above is only the worst field. A set mixing meters, radians and
	# m/s hides which one moved, and a field excluded from triggering can diverge
	# freely without ever showing up as a correction, so each is logged on its own.
	for field: StringName in _handle.last_field_divergence:
		_file.store_line("FIELD,%d,%d,%s,%.4f" % [
			now, _clock.tick, field, _handle.last_field_divergence[field],
		])


# The car's own ground contact flag, sampled per frame, so a run can be split
# into airborne and grounded stretches without replaying it.
func _grounded() -> bool:
	if not is_instance_valid(_body):
		return false
	return bool(_body.get(&"colliding"))


func _magnitude(delta: Variant) -> float:
	match typeof(delta):
		TYPE_FLOAT:
			return absf(delta as float)
		TYPE_VECTOR2:
			return (delta as Vector2).length()
		TYPE_VECTOR3:
			return (delta as Vector3).length()
	return 0.0


func _counters() -> Dictionary:
	return {
		"consumed": _handle.consumed_count,
		"missing": _handle.missing_count,
		"starved": _handle.starved_count,
		"held": _handle.held_count,
		"drained": _handle.drained_count,
		"resync": _handle.resync_count,
		"skipped": _handle.skipped_count,
		"corrections": _handle.corrections,
	}


func _snapshot_counters() -> void:
	_previous = _counters()


# Turns the running totals into this frame's deltas and re-bases for the next.
func _counter_deltas() -> Dictionary:
	var now := _counters()
	var out: Dictionary = { }
	for key: String in now:
		out[key] = now[key] - int(_previous.get(key, 0))
	_previous = now
	return out
