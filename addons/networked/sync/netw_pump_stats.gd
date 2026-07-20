## Counters one interpolation pump emits instead of logging from inside a kernel.
##
## A kernel never warns, traces, or signals. It folds counters into a
## [NetwPumpStats] the shell publishes after the pump, so the same record is the
## Tracy plot source, the debug read, and a correctness observable the display
## calculus asserts on. Every field is order free, so a partitioned pump merges
## its per slice records by summing counters and taking the maxima.
## [codeblock]
## stats.runtimes += 1
## if starving: stats.starving += 1
## stats.max_display_lag = maxf(stats.max_display_lag, playhead.display_lag)
## [/codeblock]
class_name NetwPumpStats
extends RefCounted

## Runtimes advanced this pump.
var runtimes := 0

## Runtimes with no recorded tick after the playhead.
var starving := 0

## Histories that stopped writing because their stream went quiet.
var sleeping := 0

## Channels sampled past the newest tick under a forecasting playhead.
var projecting := 0

## Snap escapes taken instead of a smoothed step.
var snaps := 0

## Largest display lag any runtime carried, in ticks.
var max_display_lag := 0.0

## Largest number of ticks any channel projected past its newest sample.
var max_forecast_age := 0.0


## Resets every counter so one record can be reused across pumps without allocating.
func reset() -> void:
	runtimes = 0
	starving = 0
	sleeping = 0
	projecting = 0
	snaps = 0
	max_display_lag = 0.0
	max_forecast_age = 0.0


## Folds [param other] into this record, the route order merge a partitioned pump
## runs at its join. Counters sum and maxima take the larger value.
func merge(other: NetwPumpStats) -> void:
	runtimes += other.runtimes
	starving += other.starving
	sleeping += other.sleeping
	projecting += other.projecting
	snaps += other.snaps
	max_display_lag = maxf(max_display_lag, other.max_display_lag)
	max_forecast_age = maxf(max_forecast_age, other.max_forecast_age)
