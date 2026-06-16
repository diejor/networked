@tool
## Drives client-side prediction and server reconciliation for one entity.
##
## The owning client predicts each tick and reconciles on the server's input
## acknowledgment, never on tick equality, so there is no clock lead and no
## determinism contract. Each entity has at most one input-owning peer, so the
## timeline has exactly one writer per side and reconciliation replays only this
## entity. Remote entities are display only and never simulate here.
##
## [codeblock]
## owning client, every tick t:
##     input := input_sync.snapshot_payload()
##     timeline.record_input(t, input)
##     _network_tick(input, dt, t, is_fresh = true)      # predict now
##     timeline.record_state(t + 1, captured)
##
## owning client, on state(tick, ack):
##     if diverged(predicted_at(ack + 1), authoritative):
##         restore authoritative; replay inputs_in_range(ack + 1, now)
##     timeline.trim_before(ack)
## [/codeblock]
##
## Wires onto [NetwEntity] like the save component: it owns the entity
## [NetwTimeline], binds the [StateSynchronizer] and [InputSynchronizer], and runs
## the [code]_network_tick[/code] contract on the entity root through
## [member simulate]. The per-tick step is driven by [MultiplayerSimulation] in a
## deterministic order, reconciliation is driven by
## [member StateSynchronizer.on_state_received].
class_name PredictionComponent
extends NetwComponent

## Per-entity role, resolved from authority at spawn and on control transfer.
enum Role {
	## A remote client controls the entity. Predicts and reconciles on ack.
	PREDICT,
	## The server consumes a remote peer's received input into authoritative state.
	CONSUME,
	## A listen-server host controls the entity. Simulates authoritatively from
	## local input each tick, so it neither predicts nor reconciles against itself.
	HOST_LOCAL,
	## A remote display. Never simulates here, the interpolator shows it.
	REMOTE,
}

## What the server does for a missing input tick.
enum MissingInput {
	## No input, no movement. The honest cs-style default.
	STALL,
	## Carry the last input forward over the gap.
	REPEAT_LAST,
}

## Divergence above which a state receive triggers a correction.
@export var divergence_epsilon: float = 0.01

## Server policy for a missing input tick. See [enum MissingInput].
@export var missing_policy: MissingInput = MissingInput.STALL

## The simulation step, defaulting to the entity root's
## [code]_network_tick(input, delta, tick, is_fresh)[/code]. A delegating node may
## set its own callable. It is a single callable, never a fan-out, so exactly one
## authoritative step runs per entity per tick.
var simulate: Callable = Callable()

## True while a correction is restoring and replaying. Game-feel code reads it.
var is_reconciling: bool = false

## Total corrections applied since spawn. Surfaced through
## [method MultiplayerSimulation.metrics].
var corrections: int = 0

## Deepest replay window walked by any correction.
var max_replay_depth: int = 0

## Inputs the server consumed into authoritative state. Surfaced through
## [method MultiplayerSimulation.metrics].
var consumed_count: int = 0

## Input ticks the server stepped over as lost. Surfaced through
## [method MultiplayerSimulation.metrics].
var missing_count: int = 0

## Emitted after a correction, with the divergence that triggered it.
signal reconciled(error: float)

## Emitted on every state receive on the owning client, before the trim, with
## the full divergence including sub-epsilon values. The debug overlay and the
## test observer read this to follow the whole divergence series, not just the
## corrections that [signal reconciled] reports.
signal state_evaluated(recv_tick: int, ack: int, divergence: float, corrected: bool)

var _entity: NetwEntity
var _role: Role = Role.REMOTE
var _timeline: NetwTimeline
var _clock: MultiplayerClock
var _sim: MultiplayerSimulation
var _tick_delta: float = 1.0 / 60.0

# Predict cursor.
var _latest_input_tick: int = -1
# Consume cursors.
var _next_input_tick: int = -1
var _ack: int = -1
var _last_input: Dictionary = { }


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		var entity := NetwEntity.of(self)
		if not entity:
			return
		_entity = entity
		entity.prediction = self
		if not entity.control_changed.is_connected(_on_control_changed):
			entity.control_changed.connect(_on_control_changed)


# Wire once the component is in tree (authority is already applied by the entity
# lifecycle before sibling _ready). The spawning signal fires before this child
# enters the tree, so it is not a safe wiring point.
func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_rewire()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	_unregister_from_sim()


## Steps prediction or consumption for [param tick]. Called by
## [MultiplayerSimulation] in deterministic order.
func simulate_tick(delta: float, tick: int) -> void:
	match _role:
		Role.PREDICT:
			_predict_step(delta, tick)
		Role.CONSUME:
			_consume_step(delta, tick)
		Role.HOST_LOCAL:
			_host_local_step(delta, tick)


## Returns the deterministic sort key for the simulation loop.
func order_key() -> String:
	return str(_entity.entity_id) if _entity else ""


func _on_control_changed(_previous_peer: int, _peer: int) -> void:
	_rewire()


# Resolves the role from current authority and binds the synchronizers. Idempotent
# so it can run again on control transfer.
func _rewire() -> void:
	if Engine.is_editor_hint() or not _entity or not is_inside_tree():
		return
	var state_sync := _entity.state
	var input_sync := _entity.input
	if not state_sync or not input_sync:
		return

	_unregister_from_sim()
	state_sync.on_state_received = Callable()
	state_sync.timeline = null
	state_sync.write_through = true
	input_sync.on_input_received = Callable()
	input_sync.timeline = null
	_timeline = null

	_clock = get_multiplayer_clock()
	if _clock:
		_tick_delta = _clock.ticktime
	_sim = get_service(MultiplayerSimulation) as MultiplayerSimulation
	if not simulate.is_valid():
		var root := _entity.owner
		if root and root.has_method(&"_network_tick"):
			simulate = Callable(root, &"_network_tick")

	_role = _resolve_role()
	match _role:
		Role.PREDICT:
			# Owning client owns a local predicted timeline. The server's
			# authoritative history lives in the registry, never here.
			_timeline = NetwTimeline.new()
			_entity.timeline = _timeline
			state_sync.write_through = false
			state_sync.on_state_received = _on_state
			_latest_input_tick = -1
			_register_with_sim()
		Role.CONSUME:
			# Server reads the registry timeline; the recorder writes state into it.
			_timeline = _registry_timeline()
			input_sync.timeline = _timeline
			input_sync.on_input_received = _on_server_input
			_next_input_tick = -1
			_ack = -1
			_last_input = { }
			_register_with_sim()
		Role.HOST_LOCAL:
			_timeline = _registry_timeline()
			_register_with_sim()
		Role.REMOTE:
			pass


# Server roles read the registry-owned timeline, get-or-creating it so the order
# of StateSynchronizer and PredictionComponent wiring does not matter.
func _registry_timeline() -> NetwTimeline:
	if _sim:
		return _sim.register_timeline(_entity)
	return null


func _resolve_role() -> Role:
	var is_server := multiplayer != null and multiplayer.is_server()
	if _entity.is_controlled_locally:
		return Role.HOST_LOCAL if is_server else Role.PREDICT
	if is_server:
		return Role.CONSUME
	return Role.REMOTE


# ---------------------------------------------------------------------------
# Predict (owning client)
# ---------------------------------------------------------------------------


func _predict_step(delta: float, tick: int) -> void:
	var input := _entity.input.snapshot_payload()
	_timeline.record_input(tick, input)
	_latest_input_tick = tick
	_entity.input.authored_tick = tick
	_run(input, delta, tick, true)
	_timeline.record_state(tick + 1, _capture())


func _on_state(recv_tick: int, ack: int, payload: Dictionary) -> void:
	if ack < 0:
		return
	var predicted := _timeline.latest_state_at_or_before(ack + 1)
	var divergence := INF
	if not predicted.is_empty():
		divergence = _divergence(predicted, payload)

	var corrected := divergence > divergence_epsilon
	if corrected:
		is_reconciling = true
		corrections += 1
		_restore(payload)
		# Anchor the authoritative state at its keyed tick so a later packet
		# carrying the same ack compares against the corrected value, not the
		# stale prediction it just replaced. Without this a duplicate ack (the
		# server is input starved and re-sends the same ack) re-triggers this
		# correction every tick until the ack advances past the stale entry.
		_timeline.record_state(ack + 1, payload)
		var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
		max_replay_depth = maxi(max_replay_depth, window.size())
		for entry in window:
			_run(entry["input"], _tick_delta, entry["tick"], false)
			_timeline.record_state(entry["tick"] + 1, _capture())
		reconciled.emit(divergence)
		is_reconciling = false

	_timeline.trim_before(ack)
	state_evaluated.emit(recv_tick, ack, divergence, corrected)


# ---------------------------------------------------------------------------
# Host-local (listen-server host controlling its own entity)
# ---------------------------------------------------------------------------


func _host_local_step(delta: float, tick: int) -> void:
	# The host is the authority and the controller at once, so it simulates from
	# its own gathered input and publishes the result. No prediction, no
	# reconciliation against itself.
	var input := _entity.input.snapshot_payload()
	if _timeline:
		_timeline.record_input(tick, input)
	_run(input, delta, tick, true)
	var state_sync := _entity.state
	state_sync.authored_tick = tick
	state_sync.server_ack = tick
	# Authoritative state is recorded by MultiplayerSimulation after the tick.


# ---------------------------------------------------------------------------
# Consume (server)
# ---------------------------------------------------------------------------


func _on_server_input(tick: int, _input: Dictionary) -> void:
	# The input is already in the timeline (the synchronizer recorded it before
	# this hook). Just open the consume cursor on the first received tick.
	if _next_input_tick < 0:
		_next_input_tick = tick


func _consume_step(delta: float, server_tick: int) -> void:
	if _next_input_tick >= 0:
		_consume_one(delta)
	var state_sync := _entity.state
	state_sync.authored_tick = server_tick
	state_sync.server_ack = _ack
	# Authoritative state is recorded by MultiplayerSimulation after the tick.


func _consume_one(delta: float) -> void:
	if _timeline.has_input_at(_next_input_tick):
		var input := _timeline.input_at(_next_input_tick)
		_run(input, delta, _next_input_tick, true)
		_last_input = input
		_ack = _next_input_tick
		_next_input_tick += 1
		consumed_count += 1
		return

	# A later input arrived, so this tick's input is genuinely lost. Apply the
	# policy and step over the hole. Otherwise it simply has not arrived yet.
	if _timeline.newest_input_tick() > _next_input_tick:
		missing_count += 1
		if missing_policy == MissingInput.REPEAT_LAST and not _last_input.is_empty():
			_run(_last_input, delta, _next_input_tick, true)
		# STALL applies no movement at all.
		_ack = _next_input_tick
		_next_input_tick += 1


# ---------------------------------------------------------------------------
# Shared
# ---------------------------------------------------------------------------


func _run(input: Dictionary, delta: float, tick: int, is_fresh: bool) -> void:
	if simulate.is_valid():
		simulate.call(input, delta, tick, is_fresh)


func _capture() -> Dictionary:
	return _entity.state.snapshot_payload()


func _restore(payload: Dictionary) -> void:
	_entity.state.apply_payload(payload)


# Largest per-property error between a predicted and an authoritative snapshot.
# A missing key or a non-numeric mismatch forces a correction.
func _divergence(predicted: Dictionary, authoritative: Dictionary) -> float:
	var worst := 0.0
	for key: StringName in authoritative:
		if not predicted.has(key):
			return INF
		worst = maxf(worst, _value_error(predicted[key], authoritative[key]))
	return worst


func _value_error(a: Variant, b: Variant) -> float:
	if a is Vector2 and b is Vector2:
		return (a - b).length()
	if a is Vector3 and b is Vector3:
		return (a - b).length()
	if (a is float or a is int) and (b is float or b is int):
		return absf(float(a) - float(b))
	return 0.0 if a == b else INF


func _register_with_sim() -> void:
	if _sim:
		_sim.register(self)


func _unregister_from_sim() -> void:
	if is_instance_valid(_sim):
		_sim.unregister(self)
	_sim = null
