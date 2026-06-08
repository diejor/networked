## Debuggable helper for advancing a [MultiplayerClock] in tests.
##
## The stepper owns frame estimation and timeout diagnostics. Harnesses provide
## the [SceneTree] and [MultiplayerClock] they want to drive.
class_name MultiplayerClockStepper
extends RefCounted

var tree: SceneTree
var clock: MultiplayerClock
var physics_fps := 60
var fallback_tickrate := 30
var timeout_frames := 100


func _init(
		p_tree: SceneTree,
		p_clock: MultiplayerClock,
		p_physics_fps: int = 60,
		p_fallback_tickrate: int = 30,
) -> void:
	tree = p_tree
	clock = p_clock
	physics_fps = p_physics_fps
	fallback_tickrate = p_fallback_tickrate


## Advances [member tree] until [member clock] reaches [param ticks].
func sync_ticks(ticks: int) -> void:
	assert(ticks >= 0, "sync_ticks: ticks must be non-negative.")
	if ticks == 0:
		return

	var target_tick := clock.tick + ticks
	var tickrate := clock.tickrate if clock.tickrate > 0 else fallback_tickrate
	var estimated_frames := ceili(
		float(ticks) * float(physics_fps) / float(tickrate),
	)

	await _advance_bulk_frames(maxi(0, estimated_frames - 2))
	await _advance_until_tick(target_tick, ticks, tickrate)


func _advance_bulk_frames(frames: int) -> void:
	for i in frames:
		await tree.process_frame


func _advance_until_tick(
		target_tick: int,
		requested_ticks: int,
		tickrate: int,
) -> void:
	var remaining := timeout_frames
	while clock.tick < target_tick and remaining > 0:
		await tree.process_frame
		remaining -= 1

	assert(
		clock.tick >= target_tick,
		(
				"sync_ticks timed out. Current %d. Target %d. "
				+ "Requested %d. Tickrate %d."
		) % [clock.tick, target_tick, requested_ticks, tickrate],
	)
