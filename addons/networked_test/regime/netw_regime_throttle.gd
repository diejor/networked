## Pins the main loop to a scripted rate schedule, reproducing a starved peer.
##
## The condition that produced the lag-compensation campaign's second
## generator was a backgrounded window running the host's main loop at a few
## frames per second. The mechanism is engine arithmetic, not rendering:
## physics catch-up is capped per main-loop iteration, so physics holds its
## rate down to [code]physics_hz / max_physics_steps_per_frame[/code] frames
## per second and degrades below that. Sleeping inside [method Node._process]
## reproduces the throttle deliberately, headless or rendered, and measures
## what each phase actually achieved so the run can prove its regime held.
## [codeblock]
## var throttle := NetwRegimeThrottle.new()
## throttle.phases = NetwRegimeThrottle.parse("0:5,6:20,0:5")
## add_child(throttle)          # healthy 5 s, starved at 6 fps 20 s, healthy
## ...
## var measured: Array = throttle.report()   # per phase: target, fps, frames
## [/codeblock]
class_name NetwRegimeThrottle
extends Node

## The schedule, one [Dictionary] per phase:
## [code]{ fps: float, seconds: float }[/code]. An [code]fps[/code] of zero
## leaves the main loop free for that phase.
var phases: Array[Dictionary] = []

var _phase_index: int = -1
var _phase_started_usec: int = 0
var _phase_frames: int = 0
var _last_frame_usec: int = 0
var _measured: Array[Dictionary] = []


## Parses a schedule string of comma-separated [code]fps:seconds[/code]
## pairs, [code]"0:5,6:20,0:5"[/code], into [member phases]. A malformed pair
## is skipped rather than guessed at.
static func parse(spec: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for pair in spec.split(",", false):
		var parts := pair.split(":")
		if parts.size() != 2:
			continue
		out.append(
			{
				&"fps": maxf(0.0, parts[0].to_float()),
				&"seconds": maxf(0.0, parts[1].to_float()),
			},
		)
	return out


## Returns what each phase actually achieved beside what it targeted, so a
## summary can assert the regime held rather than assume the schedule ran.
## [codeblock]
## [{ target_fps: float, seconds: float, frames: int, measured_fps: float }]
## [/codeblock]
func report() -> Array[Dictionary]:
	var out := _measured.duplicate(true)
	if _phase_index >= 0 and _phase_index < phases.size():
		out.append(_close_phase())
	return out


func _ready() -> void:
	_advance_phase()


func _process(_delta: float) -> void:
	if _phase_index >= phases.size():
		return
	var now := Time.get_ticks_usec()
	var phase := phases[_phase_index]
	if now - _phase_started_usec >= int(phase[&"seconds"] * 1_000_000.0):
		_measured.append(_close_phase())
		_advance_phase()
		if _phase_index >= phases.size():
			return
		phase = phases[_phase_index]
	_phase_frames += 1
	var fps: float = phase[&"fps"]
	if fps <= 0.0:
		_last_frame_usec = now
		return
	var period := int(1_000_000.0 / fps)
	var elapsed := now - _last_frame_usec
	if _last_frame_usec > 0 and elapsed < period:
		OS.delay_msec(int((period - elapsed) / 1000))
	_last_frame_usec = Time.get_ticks_usec()


func _advance_phase() -> void:
	_phase_index += 1
	_phase_started_usec = Time.get_ticks_usec()
	_phase_frames = 0
	_last_frame_usec = 0


func _close_phase() -> Dictionary:
	var phase := phases[_phase_index]
	var wall := float(Time.get_ticks_usec() - _phase_started_usec) / 1_000_000.0
	return {
		&"target_fps": phase[&"fps"],
		&"seconds": wall,
		&"frames": _phase_frames,
		&"measured_fps": _phase_frames / wall if wall > 0.0 else 0.0,
	}
