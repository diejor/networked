## Full ack-reconciliation rig: predicting client + consuming server.
##
## Builds matched [code]SpikePlayer[/code] nodes (a [SpikeStateSync] and a
## [SpikeInputSync]) on both peers, wires a [SpikePrediction] driver per role,
## and drives them off each peer's [MultiplayerClock]. Used by the C and D
## tiers. Latency is per direction and deterministic.
class_name SpikePredictRig
extends RefCounted

var inner: NetwTestHarness
var client: MultiplayerTree
var server_clock: MultiplayerClock
var client_clock: MultiplayerClock

var server_body: Node2D
var client_body: Node2D
var server: SpikePrediction
var predictor: SpikePrediction

var _tree: SceneTree
var _tickrate: int
var _controller_id: int
var _stepper: LockstepStepper


## Builds the rig. [param input_source] is the client's scripted input
## [code]func(tick) -> Dictionary[/code].
func setup(
		suite: NetwTestSuite,
		input_source: Callable,
		tickrate: int = 30,
		display_offset: int = 3,
		managed: bool = true,
) -> void:
	_tree = Engine.get_main_loop() as SceneTree
	_tickrate = tickrate
	inner = suite.make_harness() if managed else suite.make_unmanaged_harness()
	await inner.setup()
	client = await inner.add_client()
	server_clock = await inner.add_clock(tickrate, display_offset)
	client_clock = client.get_service(MultiplayerClock) as MultiplayerClock
	_controller_id = client.multiplayer_peer.get_unique_id()

	server_body = _build_node()
	inner.server().add_child(server_body)
	client_body = _build_node()
	client.add_child(client_body)
	await _tree.process_frame

	var dt := 1.0 / float(tickrate)

	server = SpikePrediction.new()
	server.role = SpikePrediction.Role.CONSUME
	server.dt = dt
	server.body = server_body
	server.timeline = SpikeTimeline.new()
	server.state_sync = server_body.get_node("StateSync")
	server.input_sync = server_body.get_node("InputSync")
	server.attach(server_clock)

	predictor = SpikePrediction.new()
	predictor.role = SpikePrediction.Role.PREDICT
	predictor.dt = dt
	predictor.body = client_body
	predictor.timeline = SpikeTimeline.new()
	predictor.state_sync = client_body.get_node("StateSync")
	predictor.input_sync = client_body.get_node("InputSync")
	predictor.input_source = input_source
	predictor.attach(client_clock)


## Advances both clocks by [param n] network ticks, in-process, no real frames.
func sync_ticks(n: int) -> void:
	if _stepper == null:
		_stepper = LockstepStepper.new(
			[server_clock, client_clock] as Array[MultiplayerClock],
			[inner.server().multiplayer, client.multiplayer] as Array[MultiplayerAPI],
			inner.session(),
			_tickrate,
		)
	_stepper.sync_ticks(n)


## Installs an exact inbound delay (polls) on both directions.
func delay_both(
		delay_polls: int,
		jitter_polls: int = 0,
		loss: float = 0.0,
) -> void:
	var client_peer := client.multiplayer_peer as LocalMultiplayerPeer
	var server_peer := inner.server().multiplayer_peer as LocalMultiplayerPeer
	var down := _conditions(delay_polls, 11, jitter_polls, loss)
	var up := _conditions(delay_polls, 22, jitter_polls, loss)
	inner.session().set_link_conditions(client_peer, down, 1)
	inner.session().set_link_conditions(server_peer, up, _controller_id)


func _conditions(
		delay_polls: int,
		_seed: int,
		jitter_polls: int,
		loss: float,
) -> LocalLoopbackSession.LinkConditions:
	var conditions := LocalLoopbackSession.LinkConditions.new(_seed)
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	conditions.latency_ms = float(delay_polls) * period
	conditions.jitter_ms = float(jitter_polls) * period
	conditions.packet_loss = loss
	return conditions


func _build_node() -> Node2D:
	var node := Node2D.new()
	node.name = "SpikePlayer"

	var state := SpikeStateSync.new()
	state.name = "StateSync"
	state.bundled = true
	node.add_child(state)
	state.owner = node
	state.root_path = state.get_path_to(node)

	var input := SpikeInputSync.new()
	input.name = "InputSync"
	input.controller_id = _controller_id
	node.add_child(input)
	input.owner = node
	input.root_path = input.get_path_to(node)

	return node
