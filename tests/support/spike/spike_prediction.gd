## Proto prediction/consumption driver for the lag-comp spike (architecture P3/P4).
##
## One [SpikePrediction] per role drives the ack-based reconciliation loop:
## the controlling client predicts immediately and reconciles on ack, the server
## consumes input and stamps the ack. Both run the same [method _network_tick]
## contract. Metrics ([member corrections], [member max_replay_depth],
## [member fire_count], [member divergence_log]) are the assertions the C and D
## tiers read. Deleted when Phase 0 lands.
class_name SpikePrediction
extends RefCounted

enum Role { PREDICT, CONSUME }
enum MissingInput { STALL, REPEAT_LAST }

var role: Role
var body: Node2D
var dt: float = 1.0 / 30.0
var timeline: SpikeTimeline
var state_sync: SpikeStateSync
var input_sync: SpikeInputSync
var input_source: Callable = Callable()
var missing_policy: MissingInput = MissingInput.STALL
var divergence_epsilon: float = 0.01

## One-shot offset the server applies to its body to force a divergence.
var pending_perturbation: Vector2 = Vector2.ZERO

var corrections: int = 0
var last_replay_depth: int = 0
var max_replay_depth: int = 0
var fire_count: int = 0
var sim_calls: int = 0
var last_divergence: float = 0.0
var divergence_log: Array = []
var consumed_count: int = 0
var missing_count: int = 0

var _server_input_tl: SpikeTimeline
var _next_input_tick: int = -1
var _last_input: Dictionary = {}
var _ack: int = -1
var _latest_input_tick: int = -1


## Wires this driver into [param clock] for its [member role].
func attach(clock: MultiplayerClock) -> void:
	if role == Role.PREDICT:
		state_sync.write_through = false
		state_sync.timeline = null
		state_sync.on_state_received = _on_state
		clock.on_tick.connect(_predict_tick)
	else:
		init_consume_standalone()
		input_sync.on_input_received = _on_server_input
		clock.on_tick.connect(_consume_tick)


## Prepares the server input buffer without a clock, for unit-style tests.
func init_consume_standalone() -> void:
	role = Role.CONSUME
	_server_input_tl = SpikeTimeline.new()


## Feeds a received [param input] at [param tick] into the consume buffer.
func feed_input(tick: int, input: Dictionary) -> void:
	_on_server_input(tick, input)


## Runs one consume step for [param server_tick].
func consume_step(server_tick: int) -> void:
	_consume_tick(0.0, server_tick)


func _network_tick(input: Dictionary, _tick: int, is_fresh: bool) -> void:
	sim_calls += 1
	body.position = SpikeSim.integrate(body.position, input, dt)
	if is_fresh and SpikeSim.wants_fire(input):
		fire_count += 1


func _predict_tick(_delta: float, tick: int) -> void:
	var input: Dictionary = input_source.call(tick)
	input = input.duplicate()
	input[&"tick"] = tick
	timeline.record_input(tick, input)
	_latest_input_tick = tick
	_network_tick(input, tick, true)
	timeline.record_state(tick + 1, {&"position": body.position})
	input_sync.authored_input = input


func _on_server_input(tick: int, input: Dictionary) -> void:
	_server_input_tl.record_input(tick, input)
	if _next_input_tick < 0:
		_next_input_tick = tick


func _consume_tick(_delta: float, server_tick: int) -> void:
	if pending_perturbation != Vector2.ZERO:
		body.position += pending_perturbation
		pending_perturbation = Vector2.ZERO

	if _next_input_tick >= 0:
		_consume_one()
	_publish_state(server_tick)


func _consume_one() -> void:
	if _server_input_tl.has_input_at(_next_input_tick):
		var input := _server_input_tl.input_at(_next_input_tick)
		_network_tick(input, _next_input_tick, true)
		_last_input = input
		_ack = _next_input_tick
		_next_input_tick += 1
		consumed_count += 1
		return

	# A later input arrived, so this tick's input is lost. Apply the policy and
	# step over the hole. Otherwise the input has simply not arrived yet.
	if _server_input_tl.newest_input_tick() > _next_input_tick:
		missing_count += 1
		if missing_policy == MissingInput.REPEAT_LAST and not _last_input.is_empty():
			_network_tick(_last_input, _next_input_tick, true)
		# STALL applies no movement at all.
		_ack = _next_input_tick
		_next_input_tick += 1


func _publish_state(server_tick: int) -> void:
	if state_sync == null:
		return
	state_sync.authored_tick = server_tick
	state_sync.server_ack = _ack
	if timeline != null:
		timeline.record_state(server_tick, {&"position": body.position})


func _on_state(recv_tick: int, ack: int, auth_pos: Vector2) -> void:
	if ack < 0:
		return
	var predicted := timeline.latest_state_at_or_before(ack + 1)
	var divergence := INF
	if not predicted.is_empty():
		divergence = (auth_pos - (predicted[&"position"] as Vector2)).length()
	last_divergence = divergence
	divergence_log.append(
		{"tick": recv_tick, "ack": ack, "divergence": divergence},
	)

	if divergence > divergence_epsilon:
		corrections += 1
		body.position = auth_pos
		var window := timeline.inputs_in_range(ack + 1, _latest_input_tick)
		last_replay_depth = window.size()
		max_replay_depth = maxi(max_replay_depth, window.size())
		for entry: Dictionary in window:
			_network_tick(entry["input"], entry["tick"], false)
			timeline.record_state(entry["tick"] + 1, {&"position": body.position})

	timeline.trim_before(ack)
