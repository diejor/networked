## Drives clocked peers on real physics frames with explicit tick-schedule
## faults.
##
## Each clock follows its natural physics-to-tick ratio unless a frame override
## replaces that clock's emitted tick count. A count of [code]0[/code] injects a
## zero-tick frame and [code]2[/code] injects a double-tick frame. Pair faults
## in the caller when the run must finish with matched clock ticks.
##
## [codeblock]
## var stepper := ScheduleFaultStepper.new(get_tree(), clocks)
## stepper.override_step_count(20, 1, 2)
## stepper.override_step_count(21, 1, 0)
## await stepper.sync_frames(40)
## [/codeblock]
class_name ScheduleFaultStepper
extends RefCounted

var tree: SceneTree
var clocks: Array[NetwClockInterface] = []

## The last physics frame advanced by [method sync_frames].
var frame_index := 0

var _ratios: Array[int] = []
var _overrides: Dictionary[int, Dictionary] = { }


func _init(p_tree: SceneTree, p_clocks: Array[NetwClockInterface]) -> void:
	tree = p_tree
	clocks = p_clocks
	for clock in clocks:
		_ratios.append(maxi(1, int(round(clock.physics_factor))))


## Replaces the natural tick count for [param clock_index] on absolute physics
## [param frame] with [param step_count]. Frames are one-based across all calls
## to [method sync_frames].
func override_step_count(
		frame: int,
		clock_index: int,
		step_count: int,
) -> void:
	assert(frame > frame_index, "Schedule fault frame must be in the future.")
	assert(clock_index >= 0 and clock_index < clocks.size())
	assert(step_count >= 0)
	var frame_overrides: Dictionary = _overrides.get(frame, { })
	frame_overrides[clock_index] = step_count
	_overrides[frame] = frame_overrides


## Advances [param frames] real physics frames and emits each clock's natural or
## overridden tick count after its scheduled frame.
func sync_frames(frames: int) -> void:
	assert(frames >= 0, "ScheduleFaultStepper.sync_frames: frames >= 0.")
	if frames == 0:
		return

	for clock in clocks:
		clock.manual_tick = true

	for _frame in frames:
		await tree.physics_frame
		frame_index += 1
		var frame_overrides: Dictionary = _overrides.get(frame_index, { })
		for i in clocks.size():
			var natural_count := 1 if frame_index % _ratios[i] == 0 else 0
			var step_count: int = frame_overrides.get(i, natural_count)
			clocks[i].before_tick_loop.emit()
			if step_count > 0:
				clocks[i].force_step(step_count)
			clocks[i].after_tick_loop.emit()

	for clock in clocks:
		clock.manual_tick = false
