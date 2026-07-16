## Typed registration payload for the [NetwLagCompensationInterface] engine.
##
## A [LagCompensation] node snapshots its exports into one of these and hands it
## to [method MultiplayerAPI.object_configuration_add]. The core dispatches on
## the resource type rather than the node class, so the engine is configured the
## same way whether a GDScript node, a test rig, or a future native caller
## supplies the values.
## [codeblock]
## var config := NetwLagCompensationConfig.new()
## config.max_future_action_ticks = 4
## api.object_configuration_add(lag_node, config)
## # core routes it to NetwLagCompensationInterface.configure(lag_node, config)
## [/codeblock]
class_name NetwLagCompensationConfig
extends Resource

## Maximum number of ticks a player action may be scheduled ahead of the server
## clock before it is denied.
@export_custom(0, "suffix:ticks") var max_future_action_ticks: int = 8

## Ticks a [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] action waits
## for input-backed state at its view tick before it resolves best-effort.
@export_custom(0, "suffix:ticks") var input_gate_deadline_ticks: int = 12
