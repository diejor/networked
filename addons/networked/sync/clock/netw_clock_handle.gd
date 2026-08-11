## Read-only view of the session's clock, reached through
## [member NetwMultiplayer.clock].
##
## Three surfaces answer three different questions about time, and this one only
## answers what the clock is doing right now. [MultiplayerClock] authors and
## drives the tick, [NetwClockConfig] tunes it, and every member here reflects
## what those two produced. Nothing here is settable, so a clock setting has
## exactly one way in and reading it back can never disagree with what was
## written.
## [codeblock]
## var clock := NetwMultiplayer.of(self).clock
## if clock.is_configured() and clock.is_synchronized:
##     var shown := clock.display_tick   # the tick the display is playing
##     var behind := clock.tick - shown  # ticks of buffer between them
## [/codeblock]
class_name NetwClockHandle
extends RefCounted

## The current server-calibrated simulation tick.
var tick: int:
	get:
		return _core.tick

## The duration of one simulation tick in seconds.
var ticktime: float:
	get:
		return _core.ticktime

## The fractional position within the current tick.
var tick_factor: float:
	get:
		return _core.tick_factor

## The tick the display renders, [member tick] less [member display_offset].
var display_tick: int:
	get:
		return _core.display_tick

## Multiplier that scales a velocity from physics rate to tick rate.
var physics_factor: float:
	get:
		return _core.physics_factor

## Whole physics steps one simulation tick is worth.
var physics_steps_per_tick: int:
	get:
		return _core.physics_steps_per_tick

## Whether this frame's simulated world may advance.
var is_simulating: bool:
	get:
		return _core.is_simulating

## Frames whose tick loop wanted more ticks than its budget allowed.
##
## Sustained growth here is a peer too slow to keep up with
## [member tickrate], which no other setting repairs.
var simulation_behind_count: int:
	get:
		return _core.simulation_behind_count

## Latest round trip time measurement in seconds.
var rtt: float:
	get:
		return _core.rtt

## Averaged round trip time in seconds.
var rtt_avg: float:
	get:
		return _core.rtt_avg

## Mean absolute deviation of RTT samples in seconds.
var rtt_jitter: float:
	get:
		return _core.rtt_jitter

## Estimated one-way network latency in seconds.
var one_way_latency: float:
	get:
		return _core.one_way_latency

## The [member display_offset] the measured latency and jitter call for.
##
## [signal NetwMultiplayer.display_offset_insufficient] fires when the
## configured offset sits below this.
var recommended_display_offset: int:
	get:
		return _core.recommended_display_offset

## Whether the client clock has calibrated with the server.
var is_synchronized: bool:
	get:
		return _core.is_synchronized

## Whether measured jitter is below [member jitter_stability_threshold].
var is_stable: bool:
	get:
		return _core.is_stable

## Whether a deterministic test stepper owns the tick loop instead of
## [MultiplayerClock].
var manual_tick: bool:
	get:
		return _core.manual_tick

## How many simulation ticks run per second.
##
## Set through [member NetwClockConfig.tickrate].
var tickrate: int:
	get:
		return _core.tickrate

## Maximum simulation ticks emitted in one physics frame.
##
## Set through [member NetwClockConfig.max_ticks_per_frame].
var max_ticks_per_frame: int:
	get:
		return _core.max_ticks_per_frame

## Frame delta threshold that resets the tick accumulator.
##
## Set through [member NetwClockConfig.stall_threshold].
var stall_threshold: float:
	get:
		return _core.stall_threshold

## Whether [member tick_factor] reads the engine's physics interpolation
## fraction instead of a wall-clock estimate.
##
## Set through [member NetwClockConfig.use_physics_interpolation].
var use_physics_interpolation: bool:
	get:
		return _core.use_physics_interpolation

## Strategy the local clock aligns to server time with.
##
## Set through [member NetwClockConfig.sync_mode].
var sync_mode: NetwMultiplayer.SyncMode:
	get:
		# Narrow from the engine's own enum, which mirrors this one value for value.
		return int(_core.sync_mode)

## Divergence that forces a hard clock snap.
##
## Set through [member NetwClockConfig.panic_snap_threshold].
var panic_snap_threshold: int:
	get:
		return _core.panic_snap_threshold

## Fraction of clock divergence closed per stretch frame.
##
## Set through [member NetwClockConfig.stretch_nudge_factor].
var stretch_nudge_factor: float:
	get:
		return _core.stretch_nudge_factor

## Seconds between client clock pings.
##
## Set through [member NetwClockConfig.ping_interval].
var ping_interval: float:
	get:
		return _core.ping_interval

## Ticks the client authors ahead of its half-RTT estimate.
var lead_ticks: float:
	get:
		return _core.lead_ticks

## Ticks the visual display trails the simulation.
##
## Set through [member NetwClockConfig.display_offset].
var display_offset: int:
	get:
		return _core.display_offset

## Multiplier applied to jitter in [member recommended_display_offset].
##
## Set through [member NetwClockConfig.jitter_multiplier].
var jitter_multiplier: float:
	get:
		return _core.jitter_multiplier

## Number of RTT samples retained for jitter estimation.
##
## Set through [member NetwClockConfig.jitter_window].
var jitter_window: int:
	get:
		return _core.jitter_window

## Jitter threshold below which the clock reads [member is_stable].
##
## Set through [member NetwClockConfig.jitter_stability_threshold].
var jitter_stability_threshold: float:
	get:
		return _core.jitter_stability_threshold

## Whether long-window drift metrics are logged.
##
## Set through [member NetwClockConfig.enable_drift_logging].
var enable_drift_logging: bool:
	get:
		return _core.enable_drift_logging

var _core: ClockCore


func _init(core: ClockCore) -> void:
	_core = core


## Returns whether a [MultiplayerClock] has registered its
## [NetwClockConfig] with the session.
##
## Every member here reads a default until this answers [code]true[/code], so a
## reading that looks wrong is checked against this before anything else.
func is_configured() -> bool:
	return _core.is_configured()


## Returns the measured wall-clock physics and poll cadence.
##
## A manually stepped clock has no wall-clock meaning and reports an empty
## dictionary.
## [codeblock]
## Dictionary
## ┠╴physics_frames  int    physics frames counted
## ┠╴polls           int    session polls counted
## ┠╴wall_seconds    float  seconds the count covers
## ┠╴physics_hz      float  physics frames per wall second
## ┖╴poll_hz         float  polls per wall second
## [/codeblock]
func cadence() -> Dictionary:
	return _core.cadence()
