@tool
## Drives client-side prediction and server reconciliation for one entity.
##
## The owning client predicts each tick and reconciles against
## [member StateSynchronizer.server_ack], the server's input acknowledgment, never
## against tick equality, so there is no clock lead and no determinism contract.
## Each entity has at most one input-owning peer ([member NetwEntity.controller]),
## so the [NetwTimeline] has exactly one writer per side and a correction replays
## only this entity. Remote entities are display only and never simulate here.
##
## [br][br][b]One node, four roles[/b]
## [br]The same node tree runs on every peer. [method simulate_tick] dispatches on
## [enum Role], resolved from authority at spawn, so the single node behaves as a
## predictor on the controlling client and a consumer on the server.
## [codeblock]
## func simulate_tick(delta, tick):
##     match _role:
##         Role.PREDICT:    _predict_step(delta, tick)     # client: move now, remember
##         Role.CONSUME:    _consume_step(delta, tick)      # server: replay received input
##         Role.HOST_LOCAL: _host_local_step(delta, tick)   # host: authority + controller
##         # Role.REMOTE: never simulates, the interpolator shows it
## [/codeblock]
##
## [br][b]The one ack[/b]
## [br]Two packets move. The [InputSynchronizer], whose authority follows
## [member NetwEntity.controller], ships the controlling peer's input toward the
## server stamped with [member StampedSynchronizer.authored_tick], the tick it was
## gathered. The [StateSynchronizer] ships authoritative state back stamped the same
## way and carrying [member StateSynchronizer.server_ack], surfaced to the client as
## [constant StateSynchronizer.ACK], the last input tick the server has consumed.
## That ack is the whole mechanism. It lets the client compare the server against
## its own past prediction tick for tick, instead of against the body it shows now,
## which has already raced ahead by a round trip.
## [codeblock]
## # InputSynchronizer, client -> server   (authored_tick = the tick it was gathered)
## { StampedSynchronizer.TICK: authored_tick, &"motion": ... }
## # StateSynchronizer, server -> client   (server_ack = last consumed input tick)
## { StampedSynchronizer.TICK: authored_tick, StateSynchronizer.ACK: server_ack, &"position": ... }
## [/codeblock]
##
## [b]The two loops[/b]
## [br]The client never waits. It predicts on its own input every tick and records
## both the input and the resulting state into its [NetwTimeline] through
## [method NetwTimeline.record_input] and [method NetwTimeline.record_state]. The
## server drains that input a half-trip late, so the acknowledgment it sends back,
## and that the client reconciles against, is a full round trip old by the time it
## lands. That lag is steady state, not a disagreement.
## [codeblock]
## scenario: hold right from tick 10, body moves +1/tick, half-trip = 3 ticks
##
## client tick    10   11   12   13   14   15   16
## predicted       1    2    3    4    5    6    7
##                 |-- input 10 ->|
## server tick                   13   14   15   16
## true pos                       1    2    3    4
##                                |- state(10) ->|
##                                  client gets {pos=1, ack=10} a round trip later
## [/codeblock]
## The per-role steps behind that picture:
## [codeblock]
## # controlling client, every tick; never waits on the server
## func _predict_step(delta, tick):
##     var input := _entity.input.snapshot_payload()
##     _timeline.record_input(tick, input)
##     _run(input, delta, tick, true)                # predict: move the body now
##     _timeline.record_state(tick + 1, _capture())
##
## # server, every tick; input arrives late, so the cursor trails by a round trip
## func _consume_step(delta, server_tick):
##     var input := _timeline.input_at(_next_input_tick)
##     _run(input, delta, _next_input_tick, true)    # advance the true body
##     _ack = _next_input_tick                        # accounted up to here
##     _next_input_tick += 1
##
## # controlling client, when a state packet arrives
## func _on_state(recv_tick, ack, authoritative):
##     var predicted := _timeline.latest_state_at_or_before(ack + 1)
##     if _divergence(predicted, authoritative) > divergence_epsilon:
##         _restore(authoritative)
##         for entry in _timeline.inputs_in_range(ack + 1, _latest_input_tick):
##             _run(entry.input, delta, entry.tick, false)   # replay, not fresh
##     _timeline.trim_before(ack)
## [/codeblock]
##
## [br][b]Why [code]ack + 1[/code][/b]
## [br]Applying the input gathered at [code]t[/code] produces the state recorded at
## [code]t + 1[/code], so an [constant StateSynchronizer.ACK] of [code]t[/code]
## means the resulting state is the [code]t + 1[/code] entry, which the client looks
## up with [method NetwTimeline.latest_state_at_or_before]. A prediction that holds
## produces zero corrections however stale the link is, because the client checks
## the server against the matching past entry, not against where the body has since
## raced to.
## [codeblock]
## input[t]    :   10   11   12   13      input 10 is applied across the step
## state[t+1]  :   11   12   13   14      its result is recorded at tick 11
##                  ^^
## ack = 10  ───────┘  matches state[11], so compare {pos=1} against state[11]
##                     |1 - 1| = 0  ->  no correction, though the body is at 7
## [/codeblock]
##
## [br][b]Reconciliation[/b]
## [br]The same compare catches a wrong prediction. The body snaps to the
## authoritative payload through [method StampedSynchronizer.apply_payload], then
## every input the server has not yet acked replays on top of it with
## [code]is_fresh = false[/code] so one-shot effects fire once. [member is_reconciling]
## is true across the snap and replay, and [signal reconciled] carries the
## divergence. [signal state_evaluated] reports every receive, including the
## sub-[member divergence_epsilon] ones that do not correct.
## [codeblock]
## a stun froze the true body at 3 while the client predicted on to 9
##
## predicted   :  14:4  15:5  16:6  17:7  18:8  19:9   the client ran ahead
## server says :  {pos=3, ack=13}   ->  predicted state[14] = 4,  |3-4| = 1
##
## snap body to authoritative 3, then replay inputs 14..now on top of truth:
##   3 ──► 4 ──► 5 ──► 6 ──► 7 ──► 8 ──► 9             re-recorded as it walks
## [/codeblock]
##
## [br][b]Wiring[/b]
## [br]Wires onto [NetwEntity] like the save component. On the controlling client it
## owns a local predicted [NetwTimeline] and keeps [member StampedSynchronizer.write_through]
## false so the network never snaps the predicted body. On the server it reads the
## registry timeline that [LagCompensation] records authoritative state into,
## so this component only consumes input and never writes server history itself.
## [member simulate] holds the single
## [code]_network_tick(delta, tick, is_fresh)[/code] callable, defaulting to
## the entity root. [method simulate_tick] is driven by [LagCompensation] in
## deterministic order, and reconciliation is event driven off
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
## [code]_network_tick(delta, tick, is_fresh)[/code]. A delegating node may
## set its own callable. It is a single callable, never a fan-out, so exactly one
## authoritative step runs per entity per tick.
var simulate: Callable = Callable()

## True while a correction is restoring and replaying. Game-feel code reads it.
var is_reconciling: bool = false

## Total corrections applied since spawn. Surfaced through
## [method LagCompensation.metrics].
var corrections: int = 0

## Deepest replay window walked by any correction.
var max_replay_depth: int = 0

## Inputs the server consumed into authoritative state. Surfaced through
## [method LagCompensation.metrics].
var consumed_count: int = 0

## Input ticks the server stepped over as lost. Surfaced through
## [method LagCompensation.metrics].
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
var _sim: LagCompensation
var _tick_delta: float = 1.0 / 60.0

# Predict cursor.
var _latest_input_tick: int = -1
# Consume cursors.
var _next_input_tick: int = -1
var _ack: int = -1
var _last_input: Dictionary = { }


func _init() -> void:
	name = "PredictionComponent"
	unique_name_in_owner = true


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
## [LagCompensation] in deterministic order.
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


## Returns the [NetwTimeline] state key for the latest authoritative snapshot.
##
## Remote client input applies at its authored input tick, and the resulting
## state belongs to the following state tick. Server rewind history uses that
## input-backed tick so [method LagCompensation.sample] answers the same
## timeline the predicting client records.
## [codeblock]
## input tick t -> authoritative state tick t + 1
## [/codeblock]
func history_record_tick(fallback_tick: int) -> int:
	if _role == Role.CONSUME and _ack >= 0:
		return _ack + 1
	return fallback_tick


## Returns true when this component has consumed through [param state_tick].
##
## Server-side state-ready actions use this to wait for recorded state, not just
## received input. Non-consume roles do not trail a remote input stream, so they
## are considered ready.
func has_consumed_state_tick(state_tick: int) -> bool:
	if _role != Role.CONSUME:
		return true
	return _ack >= 0 and _ack + 1 >= state_tick


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
	input_sync.write_through = true
	_timeline = null

	_clock = get_multiplayer_clock()
	if _clock:
		_tick_delta = _clock.ticktime
	# Prediction is meaningless without the tree's rewind substrate, so resolve it
	# through the required guard: it logs a clear error when this component sits
	# under a MultiplayerTree with no LagCompensation node, yet stays quiet for a
	# scene run standalone (no enclosing tree, e.g. pressing F6 to test in isolation).
	_sim = LagCompensation.resolve_required(self)
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
			# The input synchronizer reads this timeline to build its redundancy
			# window. Safe: the owning client is the input authority and never
			# receives its own input, so it only reads here.
			input_sync.timeline = _timeline
			state_sync.write_through = false
			state_sync.on_state_received = _on_state
			_latest_input_tick = -1
			_register_with_sim()
		Role.CONSUME:
			# Server reads the registry timeline; the recorder writes state into it.
			_timeline = _registry_timeline()
			input_sync.timeline = _timeline
			input_sync.on_input_received = _on_server_input
			input_sync.write_through = false
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
	_entity.input.acknowledged_tick = ack
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
		var live_input := _entity.input.snapshot_payload()
		for entry in window:
			_run(entry["input"], _tick_delta, entry["tick"], false)
			_timeline.record_state(entry["tick"] + 1, _capture())
		_entity.input.apply_payload(live_input)
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
	# Authoritative state is recorded by LagCompensation after the tick.

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
	# Authoritative state is recorded by LagCompensation after the tick.


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
	_entity.input.apply_payload(input)
	if simulate.is_valid():
		simulate.call(delta, tick, is_fresh)


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
