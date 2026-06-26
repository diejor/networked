## Drives clocked peers tick by tick, pumping real physics frames at each clock's
## natural physics-to-tick ratio.
##
## Like [LockstepStepper] it owns ticking through
## [member MultiplayerClock.manual_tick] and
## [method MultiplayerClock.force_step], so the tick count is
## exact with no dependence on wall-clock accumulation or [member Engine.
## time_scale]. Unlike [LockstepStepper] it runs real [signal SceneTree.
## physics_frame]s before each step, so frame coupled work still happens: input
## sampled in [code]_physics_process[/code], [method CharacterBody2D.
## move_and_slide], and visual interpolation in [code]_process[/code].
##
## A clock at [member MultiplayerClock.tickrate] below [member Engine.
## physics_ticks_per_second] naturally ticks once every several physics frames.
## The stepper honors that ratio so a stepped scene runs at the game's real
## cadence: per-frame work (a physics-mode [AnimationPlayer] fuse) and per-tick
## work (a tick-paced body) stay in wall-clock proportion instead of collapsing
## to one frame per tick.
##
## [codeblock]
## physics_ticks_per_second = 60, tickrate = 15  →  ratio 4
##
## frame: 1   2   3   4   5   6   7   8
##                    ^               ^
##                  tick 1          tick 2
## [/codeblock]
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
## await stepper.sync_ticks(8)   # 8 ticks at each clock's physics:tick ratio
## [/codeblock]
class_name FrameLockstepStepper
extends RefCounted

var tree: SceneTree
var clocks: Array[MultiplayerClock] = []


func _init(p_tree: SceneTree, p_clocks: Array[MultiplayerClock]) -> void:
	tree = p_tree
	clocks = p_clocks


## Advances every clock by exactly [param ticks] ticks. Each clock force steps
## once every [code]physics_ticks_per_second / tickrate[/code] physics frames, so
## a low-tickrate clock advances at its real wall-clock pace while the shared
## physics-frame stream keeps frame-coupled work moving.
## [member MultiplayerClock.manual_tick] is held only for the duration of
## the call, so the real tick loop resumes between calls and connection-phase
## resumes between calls and connection-phase handshakes keep ticking.
func sync_ticks(ticks: int) -> void:
	assert(ticks >= 0, "FrameLockstepStepper.sync_ticks: ticks >= 0.")
	if ticks == 0:
		return

	# Physics frames per game tick for each clock, clamped to at least one.
	var ratios: Array[int] = []
	var max_ratio := 1
	for clock in clocks:
		var ratio := maxi(1, int(round(clock.physics_factor)))
		ratios.append(ratio)
		max_ratio = maxi(max_ratio, ratio)
		clock.manual_tick = true

	# Run the longest clock's full span of frames, force stepping each clock on
	# the frames it would naturally tick and capping it at exactly ticks.
	var stepped := PackedInt32Array()
	stepped.resize(clocks.size())
	for frame in range(1, ticks * max_ratio + 1):
		await tree.physics_frame
		for i in clocks.size():
			if stepped[i] < ticks and frame % ratios[i] == 0:
				clocks[i].force_step(1)
				stepped[i] += 1

	for clock in clocks:
		clock.manual_tick = false
