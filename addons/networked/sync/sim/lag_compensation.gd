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
## The engine lives in [NetwLagCompensationInterface], owned by
## [NetwMultiplayer]. Registering through
## [method MultiplayerAPI.object_configuration_add] pushes this node's export
## snapshot into [member NetwMultiplayer.lag_compensation] and activates it.
## Reach the query surface through [member NetwMultiplayer.lag_compensation], never
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
## # per tick, driven by NetwClockInterface.on_tick:
## step every registered prediction engine    # predict or consume, per role
## if server: record authoritative state       # snapshot into each NetwTimeline
## [/codeblock]
##
## A server-authored state set registers its entity through
## [method NetwLagCompensationInterface.register_timeline] when it registers, so
## an entity is rewindable by default without a [PredictionComponent].
## [method NetwLagCompensationInterface.timeline_of] is the query seam, and
## [method NetwLagCompensationInterface.sample] and
## [method NetwLagCompensationInterface.rewind] read it.
##
## Registered through [NetwService] per [MultiplayerTree], like
## [MultiplayerClock], so several trees in one [SceneTree] each get their own
## loop. Mount it as a sibling of the clock under the session root.
class_name LagCompensation
extends NetwService

# Caps the per-frame clock-bind retry so a tree that never mounts a clock stops
# polling. The clock can register after this service, so the bind retries until it
# appears.
const _MAX_BIND_ATTEMPTS := 600

## Maximum number of ticks a player action may be scheduled ahead of the
## server clock before it is denied.
@export_custom(0, "suffix:ticks") var max_future_action_ticks: int = 8:
	set(v):
		max_future_action_ticks = v
		if _interface:
			_interface.max_future_action_ticks = v

## Ticks a [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] action waits
## for input-backed state at its view tick before it resolves best-effort. See
## [member NetwLagCompensationInterface.input_gate_deadline_ticks].
@export_custom(0, "suffix:ticks") var input_gate_deadline_ticks: int = 12:
	set(v):
		input_gate_deadline_ticks = v
		if _interface:
			_interface.input_gate_deadline_ticks = v

## The [NetwLagCompensationInterface] engine this node configures, or
## [code]null[/code] before registration. Consumers should reach the engine
## through [member NetwMultiplayer.lag_compensation] rather than this node.
var _interface: NetwLagCompensationInterface

# The typed payload registered with the API, retained so the matching
# object_configuration_remove passes the same resource.
var _config: NetwLagCompensationConfig

var _clock: NetwClockInterface
var _bind_attempts: int = 0


func _service_type() -> Script:
	return LagCompensation


func _service_entered(mt: MultiplayerTree) -> void:
	if mt.api:
		_interface = mt.api.lag_compensation
		_config = _build_config()
		mt.api.object_configuration_add(self, _config)
	if not mt.session_entered.is_connected(_on_session_entered):
		mt.session_entered.connect(_on_session_entered)
	if mt.is_online():
		_on_session_entered.call_deferred()
	var tree := get_tree()
	if tree and not tree.node_added.is_connected(_on_node_added):
		tree.node_added.connect(_on_node_added)


func _service_exiting(mt: MultiplayerTree) -> void:
	var tree := get_tree()
	if tree and tree.node_added.is_connected(_on_node_added):
		tree.node_added.disconnect(_on_node_added)
	_unbind_clock()
	if mt.api:
		mt.api.replication.register_channel(
			NetwFrameEnvelope.Channel.ACTION,
			Callable(),
		)
		if _config:
			mt.api.object_configuration_remove(self, _config)


# Snapshots the current exports into the typed payload the interface configures
# from. Export setters keep pushing live edits straight to the interface, so
# this runs once per registration.
func _build_config() -> NetwLagCompensationConfig:
	var config := NetwLagCompensationConfig.new()
	config.max_future_action_ticks = max_future_action_ticks
	config.input_gate_deadline_ticks = input_gate_deadline_ticks
	return config


func _on_session_entered() -> void:
	_bind_attempts = 0
	_try_bind_clock()
	var mt := MultiplayerTree.resolve(self)
	if mt and mt.api and _interface:
		mt.api.replication.register_channel(
			NetwFrameEnvelope.Channel.ACTION,
			_interface._handle_action_carrier,
		)


# Binds to the tick loop once the clock engine is configured. The clock can mount
# after this service, so a miss reschedules on the next frame until the clock
# appears or the attempt cap is reached.
func _try_bind_clock() -> void:
	if is_instance_valid(_clock):
		return
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return
	var clock := mt.api.clock if mt.api and mt.api.clock.is_configured() else null
	if clock:
		_clock = clock
		if _interface:
			_interface._clock = clock
		if not clock.on_tick.is_connected(_on_tick):
			clock.on_tick.connect(_on_tick)
		return
	_bind_attempts += 1
	if _bind_attempts <= _MAX_BIND_ATTEMPTS and is_inside_tree() \
			and not get_tree().process_frame.is_connected(_try_bind_clock):
		get_tree().process_frame.connect(_try_bind_clock, CONNECT_ONE_SHOT)


func _unbind_clock() -> void:
	if is_instance_valid(_clock) and _clock.on_tick.is_connected(_on_tick):
		_clock.on_tick.disconnect(_on_tick)
	_clock = null
	if _interface:
		_interface._clock = null


func _on_tick(delta: float, tick: int) -> void:
	if _interface:
		_interface.tick_step(delta, tick)


func _on_node_added(node: Node) -> void:
	if _interface:
		_interface._on_node_added(node)
