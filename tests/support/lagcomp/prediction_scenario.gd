## Real-node lag-comp scenario rig: host, one client, and predicted entities.
##
## Wraps [NetwTestHarness] with a [MultiplayerClock] and a mounted [LagCompensation]
## node on both peers, then composes matched [LagCompSimBody]
## pairs through
## [PlayerBuilder] so the real [StateSynchronizer], [InputSynchronizer], and
## [PredictionComponent] run end to end. A [LockstepStepper] drives both clocks
## in process, so corrections, replay depth, and divergence are deterministic and
## a scenario reads like its retired spike did.
##
## [codeblock]
## var s := PredictionScenario.new()
## await s.setup(self)
## var p := s.add_predicted_entity()
## s.latency_both(4)
## s.hold_input(p, { motion = Vector2.RIGHT })
## s.run(30)
## s.perturb_server(p, Vector2(60, -40))
## s.run(70)
## assert_int(p.corrections).is_equal(1)
## [/codeblock]
class_name PredictionScenario
extends RefCounted

const TICKRATE := 30
const DISPLAY_OFFSET := 3

var inner: NetwTestHarness
var server: MultiplayerTree
var client: MultiplayerTree
var server_clock: MultiplayerClock
var client_clock: MultiplayerClock
var server_sim: LagCompensation
var client_sim: LagCompensation

var _suite: NetwTestSuite
var _tree: SceneTree
var _stepper: LockstepStepper
var _tickrate: int
var _client_peer_id: int
var _entities: Array[PredictedEntity] = []
var _entity_counter: int = 0

## Entity-root type composed for each predicted pair through [PlayerBuilder].
##
## Must extend [LagCompSimBody] so the scripted [member LagCompSimBody.motion] and
## [member LagCompSimBody.bombing] input and the [PredictedEntity] metric slots
## still resolve. Defaults to the closed-form body. Point it at an alternate
## closed-form subclass (a future predicted-action body) to drive that through the
## same rig. A real-physics [CharacterBody2D] does not fit here: the
## [LockstepStepper] never steps physics frames, so [method CharacterBody2D.move_and_slide]
## would not advance. That replay path lives in its own single-peer fixture
## ([code]test_kinematic_replay.gd[/code]).
var body_type: Variant = LagCompSimBody


## Builds the scenario: host, one client, clocks, simulations, and the stepper.
##
## Pass [code]managed = false[/code] to own teardown explicitly (the determinism
## suite runs the scenario twice in one case).
func setup(
		suite: NetwTestSuite,
		tickrate: int = TICKRATE,
		display_offset: int = DISPLAY_OFFSET,
		managed: bool = true,
) -> void:
	_suite = suite
	_tickrate = tickrate
	_tree = Engine.get_main_loop() as SceneTree
	inner = suite.make_harness() if managed else suite.make_unmanaged_harness()
	await inner.setup()
	client = await inner.add_client()
	server = inner.server()
	server_clock = await inner.add_clock(tickrate, display_offset)
	client_clock = client.get_service(MultiplayerClock) as MultiplayerClock
	server_clock.manual_tick = true
	client_clock.manual_tick = true
	_client_peer_id = client.multiplayer_peer.get_unique_id()

	# The service is no longer auto-created, so mount the node on both peers.
	server_sim = inner.add_lag_compensation()
	client_sim = client.get_service(LagCompensation) as LagCompensation
	await _tree.process_frame

	# Freeze both clocks under lockstep so every tick is driven by run(), with no
	# stray physics-frame ticks polluting the deterministic schedule.
	_stepper = LockstepStepper.new(
		[server_clock, client_clock] as Array[MultiplayerClock],
		[server.multiplayer, client.multiplayer] as Array[MultiplayerAPI],
		inner.session(),
		tickrate,
	)


## Returns the tick duration in seconds.
func dt() -> float:
	return server_clock.ticktime


## Composes a matched predicted-entity pair, one per peer, and returns its handle.
func add_predicted_entity(
		state_props: Array[StringName] = [&"position"],
		input_props: Array[StringName] = [&"motion", &"bombing"],
		missing_policy: PredictionComponent.MissingInput = \
		PredictionComponent.MissingInput.STALL,
		epsilon: float = 0.01,
) -> PredictedEntity:
	_entity_counter += 1
	var ename := "Predicted%d" % _entity_counter
	var builder := PlayerBuilder.new(ename) \
			.with_root(body_type) \
			.with_state(state_props) \
			.with_input(input_props) \
			.with_prediction(missing_policy, epsilon)

	var server_root := builder.build() as LagCompSimBody
	var client_root := builder.build() as LagCompSimBody

	# Identity and controller pinned before tree entry so each peer resolves its
	# role (PREDICT on the client, CONSUME on the server) in _ready.
	for root: LagCompSimBody in [server_root, client_root]:
		var entity := NetwEntity.of(root)
		entity.peer_id = _client_peer_id
		entity.controller = _client_peer_id

	server.add_child(server_root)
	client.add_child(client_root)
	await _tree.process_frame

	var p := PredictedEntity.new()
	p._scenario = self
	p._suite = _suite
	p._bind(server_root, client_root)
	p._resolve_slots()
	p.observer = PredictionObserver.new()
	p.observer.observe(p.client_prediction)
	_entities.append(p)
	return p


## Advances both clocks by [param n] ticks, scripting client input each tick.
##
## [param per_tick] is an optional [code]func(tick: int)[/code] callback run after
## input is applied and before the tick steps.
func run(n: int, per_tick: Callable = Callable()) -> void:
	for _i in range(n):
		_apply_scripted_inputs(0.0, client_clock.tick)
		if per_tick.is_valid():
			per_tick.call(client_clock.tick)
		_stepper.sync_ticks(1)


## Runs ticks until [param predicate] (a [code]func() -> bool[/code]) is true or
## [param limit] ticks pass. Returns the ticks run.
func run_until(predicate: Callable, limit: int = 600) -> int:
	var ran := 0
	while ran < limit and not predicate.call():
		run(1)
		ran += 1
	return ran


## Runs [param ticks] to settle the link, then clears [param p]'s metrics so a
## test asserts on steady state.
##
## The real [InputSynchronizer] carries one stale first packet that the fully
## virtual spike doubles never modeled, so "clean delivery never corrects" holds
## only after the link warms up. The virtual [constant StampedSynchronizer.TICK]
## stamp is read live when the packet flushes, but the real payload property the
## owning client drives (here [member LagCompSimBody.motion]) is captured one send
## behind. The first packet therefore pairs a fresh stamp with the pre-authored
## value, the server consumes that wrong input once, and the owning client spends
## one RTT snapping it out before it converges. The state wire has no such skew
## (its payload rides the same delta as the stamp), so only the input side needs
## warming.
##
## [codeblock]
## predict tick 1:  authored_tick := 1 (live)   motion := RIGHT (just set)
## flush:           __tick = 1                   payload = <stale, pre-authored>
##                  server consumes (1, stale) -> one wrong input
##                  ...one RTT of corrections, then payload tracks the stamp
## [/codeblock]
##
## So a test holds its input, calls this to drain the transient, then runs and
## asserts [member PredictedEntity.corrections] is zero. [method reset_metrics]
## zeroes the counters so only post-warmup behavior is measured.
func warmup(p: PredictedEntity, ticks: int = 12) -> void:
	run(ticks)
	reset_metrics(p)


## Clears [param p]'s correction, replay, consume, and divergence counters.
func reset_metrics(p: PredictedEntity) -> void:
	p.client_prediction.corrections = 0
	p.client_prediction.max_replay_depth = 0
	p.server_prediction.consumed_count = 0
	p.server_prediction.missing_count = 0
	p.observer.divergence_log.clear()
	p.observer.correction_count = 0


## Sets [param p]'s persistent scripted input.
func hold_input(p: PredictedEntity, input: Dictionary) -> void:
	p._hold_input = _normalize(input)


## Overrides [param p]'s input for the single predict [param tick].
func set_input_at(p: PredictedEntity, tick: int, input: Dictionary) -> void:
	p._input_at[tick] = _normalize(input)


## Forces a divergence the client never predicted by nudging the server body.
func perturb_server(p: PredictedEntity, offset: Vector2) -> void:
	p.server_root.position += offset


## Feeds a received [param input] at [param tick] into [param p]'s server timeline.
##
## Drives the consume path directly for the missing-input policy cases, without a
## live client stream.
func feed_server_input(
		p: PredictedEntity,
		tick: int,
		input: Dictionary,
) -> void:
	p.server_input.record(tick, _normalize(input))


## Runs one server consume step for [param p] at [param tick].
func consume_step(p: PredictedEntity, tick: int) -> void:
	p.server_prediction.simulate_tick(dt(), tick)


## Installs a per-direction inbound latency on both links.
func latency_both(
		polls: int,
		jitter: int = 0,
		loss: float = 0.0,
) -> void:
	latency_down(polls, jitter, loss)
	latency_up(polls, jitter, loss)


## Installs server-to-client inbound latency (the state stream).
func latency_down(
		polls: int,
		jitter: int = 0,
		loss: float = 0.0,
) -> void:
	var client_peer := client.multiplayer_peer as LocalMultiplayerPeer
	inner.session().set_link_conditions(
		client_peer,
		_conditions(polls, 11, jitter, loss),
		1,
	)


## Installs client-to-server inbound latency (the input stream).
func latency_up(
		polls: int,
		jitter: int = 0,
		loss: float = 0.0,
) -> void:
	var server_peer := server.multiplayer_peer as LocalMultiplayerPeer
	inner.session().set_link_conditions(
		server_peer,
		_conditions(polls, 22, jitter, loss),
		_client_peer_id,
	)


## Tears the underlying harness down. Only needed for an unmanaged setup.
func teardown() -> void:
	await inner.teardown()
	_entities.clear()
	_stepper = null
	inner = null
	server = null
	client = null
	server_clock = null
	client_clock = null
	server_sim = null
	client_sim = null
	_suite = null
	_tree = null


func _apply_scripted_inputs(_delta: float, tick: int) -> void:
	for p in _entities:
		var input := p._input_for(tick)
		p.client_root.motion = input.get(&"motion", Vector2.ZERO)
		p.client_root.bombing = input.get(&"bombing", false)


func _normalize(input: Dictionary) -> Dictionary:
	return {
		&"motion": _read(input, &"motion", "motion", Vector2.ZERO),
		&"bombing": _read(input, &"bombing", "bombing", false),
	}


# Reads a key authored as either a StringName or a String literal.
func _read(
		input: Dictionary,
		key_sn: StringName,
		key_s: String,
		default: Variant,
) -> Variant:
	if input.has(key_sn):
		return input[key_sn]
	if input.has(key_s):
		return input[key_s]
	return default


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
