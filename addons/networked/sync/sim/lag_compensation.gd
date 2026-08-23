@icon("res://addons/networked/assets/lag-compensation.svg")
@tool
## Session service that enables temporal networking infrastructure for one tree.
##
## Lag compensation is one feature exposed as a single mounted node. Drop a
## [LagCompensation] node under the [MultiplayerTree], like [MultiplayerClock],
## and the tree becomes rewindable. A tree with no
## [LagCompensation] node cleanly opts out, so the per-entity prediction engine,
## the state-set timeline registration, and the [NetwAction] transport all degrade
## to no-ops.
## The engine lives in [NetwMultiplayer], owned by
## [NetwMultiplayer]. [method NetwMultiplayer.service_install] pushes this
## node's export snapshot into the session's lag-compensation core and
## activates it.
## Reach the query surface through [method NetwMultiplayer.lagcomp_sample],
## [method NetwMultiplayer.lagcomp_rewind], and
## [method NetwMultiplayer.lagcomp_action], never
## by node lookup.
##
## The engine keeps one authoritative [NetwTimeline] per entity as the rewind
## substrate and steps every registered prediction engine each tick in a stable
## order, so a replayed trace is reproducible. Entity-specific prediction logic
## lives in the per-entity engine record behind [member NetwEntity.prediction],
## declared by a [PredictionComponent].
##
## [codeblock]
## MultiplayerTree
## ├── MultiplayerClock
## └── LagCompensation              # drop this node to enable rewind + prediction
##
## # per tick, driven by NetwMultiplayer.on_tick:
## step every registered prediction engine    # predict or consume, per role
## if server: record authoritative state       # snapshot into each NetwTimeline
## [/codeblock]
##
## A server-authored state set registers its entity through
## [method NetwMultiplayer.timeline_declare] when it registers, so
## an entity is rewindable by default without a [PredictionComponent].
## [method NetwMultiplayer.timeline_of] is the query seam, and
## [method NetwMultiplayer.sample] and
## [method NetwMultiplayer.lagcomp_rewind] read it.
##
## Registered through [NetwService] per [MultiplayerTree], like
## [MultiplayerClock], so several trees in one [SceneTree] each get their own
## loop. Mount it as a sibling of the clock under the session root.
class_name LagCompensation
extends NetwService

## Maximum number of ticks a player action may be scheduled ahead of the
## server clock before it is denied.
@export_custom(0, "suffix:ticks") var max_future_action_ticks: int = 8:
	set(v):
		max_future_action_ticks = v
		if _interface:
			_interface.max_future_action_ticks = v

## Ticks a [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] action waits
## for input-backed state at its view tick before it resolves best-effort. See
## [member NetwMultiplayer.input_gate_deadline_ticks].
@export_custom(0, "suffix:ticks") var input_gate_deadline_ticks: int = 12:
	set(v):
		input_gate_deadline_ticks = v
		if _interface:
			_interface.input_gate_deadline_ticks = v

## The [NetwMultiplayer] engine this node configures, or
## [code]null[/code] before registration. Consumers should reach the engine
## through [method NetwMultiplayer.lagcomp_sample] rather than this node.
var _interface: NetwMultiplayer

# The typed payload registered with the API on entry, snapshotting the exports.
var _config: NetwLagCompensationConfig


func _service_type() -> Script:
	return LagCompensation


func _service_entered(api: NetwMultiplayer) -> void:
	_interface = api
	_config = _build_config()
	api.service_install(_config)
	var tree := get_tree() if is_inside_tree() else null
	if _interface and tree \
			and not tree.node_added.is_connected(_interface._on_node_added):
		tree.node_added.connect(_interface._on_node_added)

# No _service_exiting override. The channel, clock binding, node-added observer,
# and config all target the interface, so they outlive this node and a scene
# change that frees it leaves rewind running.


# Snapshots the current exports into the typed payload the interface configures
# from. Export setters keep pushing live edits straight to the interface, so
# this runs once per registration.
func _build_config() -> NetwLagCompensationConfig:
	var config := NetwLagCompensationConfig.new()
	config.max_future_action_ticks = max_future_action_ticks
	config.input_gate_deadline_ticks = input_gate_deadline_ticks
	return config
