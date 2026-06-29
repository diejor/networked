## Drives clocked loopback peers tick by tick with no real frames.
##
## Each call to [method sync_ticks] runs a synchronous loop. Per tick it forces
## exactly one [method MultiplayerClock.force_step] on every clock, advances the
## loopback delay clock by one tick period, then flushes one
## [method MultiplayerAPI.poll] per peer. Because there is exactly one send per
## tick, input and state are never thinned, so reconciliation fidelity matches a
## real run while the wall-clock cost collapses to CPU time.
##
## The stepper owns ticking: it sets [member MultiplayerClock.manual_tick] on
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

var clocks: Array[MultiplayerClock] = []
var apis: Array[MultiplayerAPI] = []
var session: LocalLoopbackSession
var tick_period_ms: float


func _init(
		p_clocks: Array[MultiplayerClock],
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
		session.advance_time(tick_period_ms)
		for api in apis:
			api.poll()
