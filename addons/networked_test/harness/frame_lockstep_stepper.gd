## Drives clocked peers tick by tick, pumping real physics frames at each clock's
## natural physics-to-tick ratio.
##
## Like [LockstepStepper] it owns ticking through
## [constant NetwMultiplayer.CLOCK_PARAM_MANUAL_TICK] and
## [method NetwMultiplayer.clock_step], so the tick count is
## exact with no dependence on wall-clock accumulation or [member Engine.
## time_scale]. Unlike [LockstepStepper] it runs real [signal SceneTree.
## physics_frame]s before each step, so frame coupled work still happens: input
## sampled in [code]_physics_process[/code], [method CharacterBody2D.
## move_and_slide], and visual interpolation in [code]_process[/code].
##
## A clock at [constant NetwMultiplayer.CLOCK_PARAM_TICKRATE] below [member Engine.
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
##     [server_session, client_session],
## )
## await stepper.sync_ticks(8)   # 8 ticks at each clock's physics:tick ratio
## [/codeblock]
class_name FrameLockstepStepper
extends RefCounted

var tree: SceneTree
var sessions: Array[NetwMultiplayer] = []


func _init(p_tree: SceneTree, p_sessions: Array[NetwMultiplayer]) -> void:
	tree = p_tree
	sessions = p_sessions


## Advances every clock by exactly [param ticks] ticks. Each clock force steps
## once every [code]physics_ticks_per_second / tickrate[/code] physics frames, so
## a low-tickrate clock advances at its real wall-clock pace while the shared
## physics-frame stream keeps frame-coupled work moving.
## [constant NetwMultiplayer.CLOCK_PARAM_MANUAL_TICK] is held only for the duration of
## the call, so the real tick loop resumes between calls and connection-phase
## resumes between calls and connection-phase handshakes keep ticking.
func sync_ticks(ticks: int) -> void:
	assert(ticks >= 0, "FrameLockstepStepper.sync_ticks: ticks >= 0.")
	if ticks == 0:
		return

	# Physics frames per game tick for each clock, clamped to at least one.
	var ratios: Array[int] = []
	var max_ratio := 1
	for api in sessions:
		var ratio := maxi(1, int(round(api.clock_get_monitor(
			NetwMultiplayer.CLOCK_MONITOR_PHYSICS_FACTOR
		))))
		ratios.append(ratio)
		max_ratio = maxi(max_ratio, ratio)
		api.clock_set_param(NetwMultiplayer.CLOCK_PARAM_MANUAL_TICK, true)

	# Run the longest clock's full span of frames, force stepping each clock on
	# the frames it would naturally tick and capping it at exactly ticks.
	var stepped := PackedInt32Array()
	stepped.resize(sessions.size())
	for frame in range(1, ticks * max_ratio + 1):
		await tree.physics_frame
		for i in sessions.size():
			sessions[i].clock_tick_loop(true)
			if stepped[i] < ticks and frame % ratios[i] == 0:
				sessions[i].clock_step(1)
				stepped[i] += 1
			sessions[i].clock_tick_loop(false)

	for api in sessions:
		api.clock_set_param(NetwMultiplayer.CLOCK_PARAM_MANUAL_TICK, false)
