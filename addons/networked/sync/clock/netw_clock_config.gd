## Typed registration payload for the [ClockCore] tick engine.
##
## A [MultiplayerClock] node snapshots its exports into one of these and hands
## it to [method NetwMultiplayer.service_install]. The session dispatches on the
## resource type rather than the node class, so nodes and code-first callers
## configure the same tick engine.
## [codeblock]
## var config := NetwClockConfig.new()
## config.tickrate = 60
## api.service_install(config)
## [/codeblock]
class_name NetwClockConfig
extends NetwObjectConfig

## How many simulation ticks to run per second.
@export_custom(0, "suffix:frames") var tickrate: int = 30

## Maximum simulation ticks allowed to run in a single physics frame.
@export_custom(0, "suffix:ticks") var max_ticks_per_frame: int = 8

## Frame delta threshold before resetting the accumulator.
@export_custom(0, "suffix:s") var stall_threshold: float = 1.0

## Reads the engine's physics interpolation fraction when available instead of a
## wall-clock estimate.
@export var use_physics_interpolation: bool = true

## Strategy used to align the local clock with the server, one of
## [enum NetwMultiplayer.SyncMode].
@export var sync_mode: NetwMultiplayer.SyncMode = NetwMultiplayer.SyncMode.SYNC_MODE_STRETCH

## The maximum allowed divergence before a hard
## [constant NetwMultiplayer.SyncMode.SYNC_MODE_SNAP] is forced.
@export_custom(0, "suffix:ticks") var panic_snap_threshold: int = 20

## Fraction of the remaining divergence the
## [constant NetwMultiplayer.SyncMode.SYNC_MODE_STRETCH] clock closes each frame.
@export_range(0.01, 0.5) var stretch_nudge_factor: float = 0.05

## How often the client pings the server to refresh RTT and recalibrate.
@export_custom(0, "suffix:s") var ping_interval: float = 0.1

## The number of ticks the visual display lags behind the simulation.
@export_custom(0, "suffix:ticks") var display_offset: int = 2

## Scales jitter impact on the recommended display offset.
@export var jitter_multiplier: float = 2.0

## Number of recent RTT samples averaged for jitter and the recommendation.
@export_custom(0, "suffix:samples") var jitter_window: int = 16

## The threshold below which the connection is considered stable.
@export_custom(0, "suffix:s") var jitter_stability_threshold: float = 0.05

## Logs average clock drift over 60-second windows to the console.
@export var enable_drift_logging: bool = false
