## Drives clocked loopback peers tick by tick with no real frames.
##
## Each call to [method sync_ticks] runs a synchronous loop. Per tick it forces
## exactly one [method NetwClockHandle.force_step] on every clock, advances the
## loopback delay clock by one tick period, then flushes one
## [method MultiplayerAPI.poll] per peer. Because there is exactly one send per
## tick, input and state are never thinned, so reconciliation fidelity matches a
## real run while the wall-clock cost collapses to CPU time.
##
## The stepper owns ticking: it sets [member NetwClockHandle.manual_tick] on
## every clock so the real [code]_physics_process[/code] loop stops
## advancing. The session is driven through
## [method LocalLoopbackSession.advance_time], not real physics frames, so
## latency stays expressed in milliseconds with no dependence on engine cadence.
##
## [codeblock]
## var stepper := LockstepStepper.new(
##     [server_clock, client_clock],
##     [server_tree.multiplayer, client_tree.multiplayer],
##     session,
##     tickrate,
## )
## stepper.sync_ticks(8)   # 8 ticks, in-process, no await
## [/codeblock]
class_name LockstepStepper
extends RefCounted

var clocks: Array[NetwClockHandle] = []
var apis: Array[MultiplayerAPI] = []
var session: LocalLoopbackSession
var tick_period_ms: float

## Physics frames per tick to drive at, overriding what each clock declares.
##
## Zero honors the declaration, which is what a correctly configured game runs
## at. Any other value models a clock whose cadence and declaration disagree,
## which is what [member NetwPredictStats.quantum_faults] counts.
var quantum_override: int = 0

# The physics frame this stepper has reached, so a clock's tick ratio is
# counted across calls rather than restarting inside each one.
var _frame: int = 0


func _init(
		p_clocks: Array[NetwClockHandle],
		p_apis: Array[MultiplayerAPI],
		p_session: LocalLoopbackSession,
		p_tickrate: int,
) -> void:
	clocks = p_clocks
	apis = p_apis
	session = p_session
	tick_period_ms = 1000.0 / float(maxi(1, p_tickrate))
	for clock in clocks:
		clock.manual_tick = true


## Advances every clock by [param ticks] ticks, releasing and flushing one
## send cycle per tick. Synchronous: returns once the ticks are done.
func sync_ticks(ticks: int) -> void:
	assert(ticks >= 0, "sync_ticks: ticks must be non-negative.")
	for _i in range(ticks):
		for clock in clocks:
			clock.force_step(1)
		_flush()


## Advances every clock by [param frames] physics frames, bracketing each one
## in the boundary a [constant NetwPredict.Schedule.FRAME] entity drives on and
## stepping the clock only on the frames it would naturally tick.
##
## [method sync_ticks] steps the clock and nothing else, which is all a
## [constant NetwPredict.Schedule.TICK] entity needs. A frame-tier entity
## authors, sends and consumes in the [signal NetwMultiplayer.after_tick_loop] pass,
## so a run that never emits the bracket leaves it at drive zero however many
## ticks it takes.
##
## The ratio is honored rather than collapsed to one tick per frame, because a
## frame-tier transition is a declared quantum of physics frames: a run that
## steps the clock every frame drives each transition in one frame where the
## clock declares [member NetwClockHandle.physics_factor], and every drive charges a
## [member NetwPredictStats.quantum_faults].
func sync_frames(frames: int) -> void:
	assert(frames >= 0, "sync_frames: frames must be non-negative.")
	var ratios := PackedInt32Array()
	var slowest := 1
	for clock in clocks:
		var ratio := quantum_override if quantum_override > 0 \
				else maxi(1, int(round(clock.physics_factor)))
		ratios.append(ratio)
		slowest = maxi(slowest, ratio)
	var frame_period_ms := tick_period_ms / float(slowest)
	for _i in range(frames):
		_frame += 1
		for i in clocks.size():
			clocks[i].begin_tick_loop()
			if _frame % ratios[i] == 0:
				clocks[i].force_step(1)
			clocks[i].end_tick_loop()
		_flush(frame_period_ms)


func _flush(period_ms: float = -1.0) -> void:
	session.advance_time(tick_period_ms if period_ms < 0.0 else period_ms)
	for api in apis:
		api.poll()
