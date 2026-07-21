class_name RacingNetLog
extends Node
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
## kind,wall_ms,tick,ticks_this_frame,ack_age,consumed,missing,starved,held,corrections
## FRAME,10233,612,1,1,1,0,0,0,0
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
## depth. A [code]ticks_this_frame[/code] of [code]0[/code] or
## [code]2[/code] is the local clock stretching to meet its calibration target.

## Environment variable that arms the recorder. Any non-empty value enables it.
const ENABLE_VAR := "NETW_NETLOG"

# Transitions held back before a journal row is written, so the row leaves with
# its fingerprint closed and its acknowledgement resolved rather than blank.
const JOURNAL_TAP_LAG := 64

const FRAME_HEADER := "kind,wall_ms,tick,ticks_this_frame,ack_age,consumed," \
		+ "missing,starved,held,folded,tape_depth,resync,skipped," \
		+ "corrections," \
		+ "contacts,grounded,drive_seq,drive_label,drive_kind,steer,throttle," \
		+ "wall_contacts,wall_impulse,wall_normal_y,car_contacts," \
		+ "ground_contacts,contact_seq"
const CORRECTION_HEADER := "kind,wall_ms,tick,field,magnitude,teleported"
const EVAL_HEADER := "kind,wall_ms,tick,recv_tick,ack,divergence,corrected"
const CONTACT_HEADER := "kind,wall_ms,tick,drive_label,contact_kind,count,impulse,normal_y"
const FIELD_HEADER := "kind,wall_ms,tick,field,error"
const JOURNAL_HEADER := "kind,wall_ms,transition,label,drive_kind,c_hash," \
		+ "post_fp,domain,flags"
const STATS_HEADER := "kind,wall_ms,consumed,missing,starved,held," \
		+ "folded,corrections,resync,skipped,max_replay_depth,fp_verified," \
		+ "fp_mismatches,first_divergent_transition,command_queue_depth," \
		+ "frames_dropped_invalid,substituted,ack_confirmed"

var _clock: NetwClockInterface
var _handle: NetwLagCompensationInterface.PredictionHandle
var _body: Node
var _inputs: Node
var _contact_probe: RigidBody3D
var _file: FileAccess
var _ticks_this_frame: int = 0
var _contacts_this_frame: int = 0
var _last_contact_sequence: int = -1
var _last_journal_transition: int = -1
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
	_inputs = _body.get_node_or_null(^"Inputs")
	_contact_probe = _body.get_node_or_null(^"Sphere") as RigidBody3D

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
	_file.store_line(JOURNAL_HEADER)
	_file.store_line(STATS_HEADER)
	print("net_log recording to %s" % ProjectSettings.globalize_path(path))

	_snapshot_counters()
	_clock.on_tick.connect(_on_tick)
	_clock.after_tick_loop.connect(_on_after_tick_loop)
	_handle.recovered.connect(_on_recovered)
	_handle.state_evaluated.connect(_on_state_evaluated)


func _exit_tree() -> void:
	if not _file:
		return
	_drain_journal(0)
	_write_stats()
	_file.close()
	_file = null


# One closing row carrying the run's totals, so a capture is summarized without
# re-deriving the counters from the per-frame deltas above it.
func _write_stats() -> void:
	var stats := _handle.stats()
	_file.store_line(
		"STATS,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d" % [
			Time.get_ticks_msec(),
			stats[&"consumed"],
			stats[&"missing"],
			stats[&"starved"],
			stats[&"held"],
			stats[&"folded"],
			stats[&"corrections"],
			stats[&"resync"],
			stats[&"skipped"],
			stats[&"max_replay_depth"],
			stats[&"fp_verified"],
			stats[&"fp_mismatches"],
			stats[&"first_divergent_transition"],
			stats[&"command_queue_depth"],
			stats[&"frames_dropped_invalid"],
			stats[&"substituted"],
			stats[&"ack_confirmed"],
		],
	)


func _on_tick(_delta: float, _tick: int) -> void:
	_ticks_this_frame += 1


# One row per physics frame, whether or not it ran a tick. A frame that ran none
# is the interesting one, so the row is written unconditionally.
func _on_after_tick_loop() -> void:
	if not _file:
		return
	_record_solver_contacts()
	var deltas := _counter_deltas()
	_file.store_line(
		(
				"FRAME,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
				+ "%d,%d,%d,%s,%.6f,%.6f,%d,%.6f,%.6f,%d,%d,%d"
		) % [
			Time.get_ticks_msec(),
			_clock.tick,
			_ticks_this_frame,
			_handle.ack_age_ticks,
			deltas["consumed"],
			deltas["missing"],
			deltas["starved"],
			deltas["held"],
			deltas["folded"],
			_handle.tape_queue_depth,
			deltas["resync"],
			deltas["skipped"],
			deltas["corrections"],
			_contacts_this_frame,
			1 if _grounded() else 0,
			_handle.drive_seq,
			_handle.last_drive_label,
			_drive_kind_name(),
			_input_value(&"steer"),
			_input_value(&"throttle"),
			_contact_int(&"wall_contacts"),
			_contact_float(&"wall_impulse"),
			_contact_float(&"wall_normal_y"),
			_contact_int(&"car_contacts"),
			_contact_int(&"ground_contacts"),
			_contact_int(&"contact_sequence"),
		],
	)
	_drain_journal(JOURNAL_TAP_LAG)
	_ticks_this_frame = 0
	_contacts_this_frame = 0


# Drains the journal rows that have settled, holding back the newest
# [param lag] transitions so a row is written only after its post-solve
# fingerprint has closed and its acknowledgement has had time to arrive. The
# journal is a bounded ring, so a run longer than its depth is recoverable only
# if the rows leave incrementally.
func _drain_journal(lag: int) -> void:
	var journal := _handle.journal()
	if not journal:
		return
	var transitions := journal.transitions()
	var settled := transitions.size() - maxi(0, lag)
	if settled <= 0:
		return
	var labels := journal.labels()
	var kinds := journal.kinds()
	var c_hashes := journal.c_hashes()
	var post_fps := journal.post_fps()
	var domains := journal.domains()
	var flags := journal.flags()
	var wall := Time.get_ticks_msec()
	for i in settled:
		if transitions[i] <= _last_journal_transition:
			continue
		_last_journal_transition = transitions[i]
		_file.store_line(
			"JOURNAL,%d,%d,%d,%d,%d,%d,%d,%d" % [
				wall,
				transitions[i],
				labels[i],
				kinds[i],
				c_hashes[i],
				post_fps[i],
				domains[i],
				flags[i],
			],
		)


# One row per corrected field, so a magnitude can be read against the epsilon
# that triggered it without parsing a nested payload. The transition and charge
# the recovery names ride the JOURNAL rows, so the CORR schema stays lean.
func _on_recovered(
		_entry: int,
		deltas: Dictionary,
		teleported: bool,
		_attribution: int,
) -> void:
	if not _file:
		return
	for field: StringName in deltas:
		_file.store_line(
			"CORR,%d,%d,%s,%.4f,%s" % [
				Time.get_ticks_msec(),
				_clock.tick,
				field,
				_magnitude(deltas[field]),
				"true" if teleported else "false",
			],
		)


## Records a contact at the current tick. Call it wherever the game notifies the
## predictor through
## [method NetwLagCompensationInterface.PredictionHandle.notify_contact], so the
## log carries the collisions that open a correction-pause window. A no-op when
## the recorder is not running.
func mark_contact(contact_kind: StringName = &"body_entered") -> void:
	_contacts_this_frame += 1
	if not _file:
		return
	_write_contact(contact_kind, 1, 0.0, 0.0)


# Records solver contact samples independently of body-entered notifications.
func _record_solver_contacts() -> void:
	var sequence := _contact_int(&"contact_sequence")
	if sequence <= _last_contact_sequence:
		return
	_last_contact_sequence = sequence
	var walls := _contact_int(&"wall_contacts")
	if walls > 0:
		_write_contact(
			&"wall_solver",
			walls,
			_contact_float(&"wall_impulse"),
			_contact_float(&"wall_normal_y"),
		)
	var cars := _contact_int(&"car_contacts")
	if cars > 0:
		_write_contact(&"car_solver", cars, 0.0, 0.0)


# Writes one classified contact row at the current tape label.
func _write_contact(
		contact_kind: StringName,
		count: int,
		impulse: float,
		normal_y: float,
) -> void:
	_file.store_line(
		"CONTACT,%d,%d,%d,%s,%d,%.6f,%.6f" % [
			Time.get_ticks_msec(),
			_clock.tick,
			_handle.last_drive_label,
			contact_kind,
			count,
			impulse,
			normal_y,
		],
	)


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
	_file.store_line(
		"EVAL,%d,%d,%d,%d,%.4f,%s" % [
			now,
			_clock.tick,
			recv_tick,
			ack,
			divergence,
			"true" if corrected else "false",
		],
	)
	# The scalar above is only the worst field. A set mixing meters, radians and
	# m/s hides which one moved, and a field excluded from triggering can diverge
	# freely without ever showing up as a correction, so each is logged on its own.
	for field: StringName in _handle.last_field_divergence:
		_file.store_line(
			"FIELD,%d,%d,%s,%.4f" % [
				now,
				_clock.tick,
				field,
				_handle.last_field_divergence[field],
			],
		)


# The car's own ground contact flag, sampled per frame, so a run can be split
# into airborne and grounded stretches without replaying it.
func _grounded() -> bool:
	if not is_instance_valid(_body):
		return false
	return bool(_body.get(&"colliding"))


func _drive_kind_name() -> String:
	var kinds := NetwLagCompensationInterface.PredictionHandle.DriveKind.keys()
	var kind := _handle.last_drive_kind
	if kind < 0 or kind >= kinds.size():
		return "UNKNOWN"
	return String(kinds[kind])


func _input_value(property: StringName) -> float:
	if not is_instance_valid(_inputs):
		return 0.0
	return float(_inputs.get(property))


func _contact_int(property: StringName) -> int:
	if not is_instance_valid(_contact_probe):
		return 0
	return int(_contact_probe.get(property))


func _contact_float(property: StringName) -> float:
	if not is_instance_valid(_contact_probe):
		return 0.0
	return float(_contact_probe.get(property))


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
		"folded": _handle.folded_count,
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
