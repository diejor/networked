## Typed registration payload for the [LagCompCore] engine.
##
## A [LagCompensation] node snapshots its exports into one of these and hands it
## to [method NetwMultiplayer.service_install]. The session dispatches on the
## resource type rather than the node class, so nodes and code-first callers
## configure the same engine.
## [codeblock]
## var config := NetwLagCompensationConfig.new()
## config.max_future_action_ticks = 4
## api.service_install(config)
## [/codeblock]
class_name NetwLagCompensationConfig
extends NetwObjectConfig

## Maximum number of ticks a player action may be scheduled ahead of the server
## clock before it is denied.
@export_custom(0, "suffix:ticks") var max_future_action_ticks: int = 8

## Ticks a [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] action waits
## for input-backed state at its view tick before it resolves best-effort.
@export_custom(0, "suffix:ticks") var input_gate_deadline_ticks: int = 12
