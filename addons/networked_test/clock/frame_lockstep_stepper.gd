## Drives clocked peers tick by tick, pumping one real frame per tick.
##
## Like [LockstepStepper] it owns ticking through [member MultiplayerClock.
## manual_tick] and [method MultiplayerClock.force_step], so the tick count is
## exact with no dependence on wall-clock accumulation or [member Engine.
## time_scale]. Unlike [LockstepStepper] it runs a real [signal SceneTree.
## physics_frame] before each step, so frame coupled work still happens: input
## sampled in [code]_physics_process[/code], [method CharacterBody2D.
## move_and_slide], and visual interpolation in [code]_process[/code].
##
## Use this for full game scenes that sample input and integrate physics through
## the engine. Use [LockstepStepper] when input is injected straight into the
## timeline and no frames are needed.
##
## [codeblock]
## var stepper := FrameLockstepStepper.new(
##     get_tree(),
##     [server_clock, client_clock],
## )
## await stepper.sync_ticks(8)   # 8 ticks, one physics frame each
## [/codeblock]
class_name FrameLockstepStepper
extends RefCounted

var tree: SceneTree
var clocks: Array[MultiplayerClock] = []


func _init(p_tree: SceneTree, p_clocks: Array[MultiplayerClock]) -> void:
	tree = p_tree
	clocks = p_clocks


## Advances every clock by [param ticks] ticks. Each tick runs one real physics
## frame then force steps every clock once. [member MultiplayerClock.manual_tick]
## is held only for the duration of the call, so the real tick loop resumes
## between calls and connection-phase handshakes keep ticking.
func sync_ticks(ticks: int) -> void:
	assert(ticks >= 0, "FrameLockstepStepper.sync_ticks: ticks >= 0.")
	if ticks == 0:
		return

	for clock in clocks:
		clock.manual_tick = true

	for _i in range(ticks):
		await tree.physics_frame
		for clock in clocks:
			clock.force_step(1)

	for clock in clocks:
		clock.manual_tick = false
