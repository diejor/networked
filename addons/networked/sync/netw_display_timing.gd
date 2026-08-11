## The per pump timing snapshot the interpolation engine reads instead of a clock.
##
## The display kernels never resolve or read a clock. The shell captures one
## [NetwDisplayTiming] from the session clock at the top of each pump and passes
## it by value into every runtime, so a runtime's sample math depends only on
## plain scalars owned by the frame. That is what lets a pump over one entity
## touch nothing outside that entity.
## [codeblock]
## var timing := NetwDisplayTiming.capture(clock, frame_delta)
## for runtime in runtimes:
##     _pump_runtime(runtime, timing)   # timing crosses by value, never a clock
## [/codeblock]
class_name NetwDisplayTiming
extends RefCounted

## The live simulation tick.
var tick := 0

## The display clock tick, already trailing the simulation by [member display_offset].
var display_tick := 0

## Fractional progress from [member display_tick] toward the next tick.
var tick_factor := 0.0

## Seconds per tick.
var ticktime := 0.0

## Ticks the display clock trails the simulation.
var display_offset := 0

## Display offset the clock recommends given measured jitter.
var recommended_display_offset := 0

## Wall clock seconds since the previous pump, the display smoothing delta.
var frame_delta := 0.0

## Ticks elapsed in this frame, [member frame_delta] times the tick rate.
var frame_ticks := 0.0


## Captures the timing a pump reads from [param clock] with [param frame_delta].
##
## Runs on the shell, the one place the clock is touched. A null clock yields a
## zeroed snapshot so a pump before any clock configurator stays inert.
static func capture(
		clock: ClockCore,
		frame_delta: float,
) -> NetwDisplayTiming:
	var timing := NetwDisplayTiming.new()
	if not clock:
		return timing
	timing.tick = clock.tick
	timing.display_tick = clock.display_tick
	timing.tick_factor = clock.tick_factor
	timing.ticktime = clock.ticktime
	timing.display_offset = clock.display_offset
	timing.recommended_display_offset = clock.recommended_display_offset
	timing.frame_delta = frame_delta
	timing.frame_ticks = frame_delta * clock.tickrate
	return timing
