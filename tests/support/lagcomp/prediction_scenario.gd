## Real-node lag-comp scenario rig: host, one client, and predicted entities.
##
## Wraps [NetwTestHarness] with a [MultiplayerClock] and a mounted [LagCompensation]
## node on both peers, then composes matched [LagCompSimBody]
## pairs through
## [PlayerBuilder] so the real derived state and input sets and the
## [PredictionComponent] run end to end. A [LockstepStepper] drives both clocks
## in process, so corrections, replay depth, and divergence are deterministic and
## a scenario reads like its retired spike did.
##
## [codeblock]
## var s := PredictionScenario.new()
## await s.setup(self)
## var p := await s.add_predicted_entity()
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
var server_clock: ClockCore
var client_clock: ClockCore
var server_sim: LagCompCore
var client_sim: LagCompCore

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
## closed-form subclass ([CarriedSimBody] is one) to drive that through the
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
		api_script: Script = null,
) -> void:
	_suite = suite
	_tickrate = tickrate
	_tree = Engine.get_main_loop() as SceneTree
	inner = suite.make_harness() if managed else suite.make_unmanaged_harness()
	inner.api_script = api_script
	await inner.setup()
	client = await inner.add_client()
	server = inner.server()
	server_clock = await inner.add_clock(tickrate, display_offset)
	client_clock = client.api._clock
	server_clock.manual_tick = true
	client_clock.manual_tick = true
	_client_peer_id = client.multiplayer_peer.get_unique_id()

	# The service is no longer auto-created, so mount the node on both peers.
	server_sim = inner.add_lag_compensation()
	client_sim = client.api._lagcomp
	await _tree.process_frame

	# Freeze both clocks under lockstep so every tick is driven by run(), with no
	# stray physics-frame ticks polluting the deterministic schedule.
	_stepper = LockstepStepper.new(
		[server_clock, client_clock] as Array[ClockCore],
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
		schedule: PredictionComponent.Schedule = \
		PredictionComponent.Schedule.TICK,
) -> PredictedEntity:
	return await _add_entity(
		_client_peer_id,
		state_props,
		input_props,
		missing_policy,
		epsilon,
		schedule,
	)


## Composes a pair the listen-server host itself controls, which resolves
## [constant NetwPredict.Role.HOST_LOCAL] on the server and
## [constant NetwPredict.Role.REMOTE] on the client.
##
## The host authors its own commands and is authority over them at once, so this
## pair has no command lane and no reconciliation. It is driven by stepping
## [member server_clock] rather than by delivering frames.
func add_host_entity(
		state_props: Array[StringName] = [&"position"],
		input_props: Array[StringName] = [&"motion", &"bombing"],
		missing_policy: PredictionComponent.MissingInput = \
		PredictionComponent.MissingInput.STALL,
		epsilon: float = 0.01,
		schedule: PredictionComponent.Schedule = \
		PredictionComponent.Schedule.TICK,
) -> PredictedEntity:
	return await _add_entity(
		MultiplayerPeer.TARGET_PEER_SERVER,
		state_props,
		input_props,
		missing_policy,
		epsilon,
		schedule,
	)


func _add_entity(
		controller: int,
		state_props: Array[StringName],
		input_props: Array[StringName],
		missing_policy: PredictionComponent.MissingInput,
		epsilon: float,
		schedule: PredictionComponent.Schedule,
) -> PredictedEntity:
	_entity_counter += 1
	var ename := "Predicted%d" % _entity_counter
	var builder := PlayerBuilder.new(ename) \
			.with_root(body_type) \
			.with_state(state_props) \
			.with_input(input_props) \
			.with_prediction(missing_policy, epsilon, schedule)

	var server_root := builder.build() as LagCompSimBody
	var client_root := builder.build() as LagCompSimBody

	# Identity and controller pinned before tree entry so each peer resolves its
	# role from the two axes in _ready. A bound entity_id declares a real
	# entity, so the rig activates LIVE (authority application, scene
	# registration) rather than staying an inert unbound node.
	for root: LagCompSimBody in [server_root, client_root]:
		var entity := NetwEntity.of(root)
		entity.entity_id = StringName(ename)
		entity.peer_id = controller
		entity.controller = controller

	server.add_child(server_root)
	client.add_child(client_root)

	# The state set is server-authored (node authority always the server) and
	# the input set is controller-authored (entity.controller, read by role
	# resolution) - two independent axes. Node authority tracking
	# entity.controller is the entity-wide default (matches REPRESENTED_PEER
	# player entities), so pin server_root's authority back explicitly rather
	# than changing that default for this rig's decoupled case.
	# SMELL(authority-pin): the state/control axes collapse onto node authority
	# today, so a server-authored client-controlled entity must re-pin. Route-keyed
	# authorship settles it later.
	server_root.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)

	NetwBench.bind_shared_route(server_root, [client_root])

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


## Advances both peers by [param n] frames, scripting client input each frame.
##
## This is [method run] for a pair composed under
## [constant NetwPredict.Schedule.FRAME]: that tier authors, sends its command
## lane and consumes in the frame boundary rather than in the tick, so a pair
## driven by [method run] stays at drive zero however many ticks it takes.
##
## [param per_frame] is an optional [code]func(tick: int)[/code] callback run
## after input is applied and before the frame steps.
func run_frames(n: int, per_frame: Callable = Callable()) -> void:
	for _i in range(n):
		_apply_scripted_inputs(0.0, client_clock.tick)
		if per_frame.is_valid():
			per_frame.call(client_clock.tick)
		_stepper.sync_frames(1)


## Drives [method run_frames] at [param frames_per_tick] physics frames per
## tick instead of at the ratio the clocks declare, which is a pair whose
## cadence and declaration disagree.
func drive_off_quantum(frames_per_tick: int) -> void:
	_stepper.quantum_override = frames_per_tick


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
## The real input stream carries one stale first packet that the fully
## virtual spike doubles never modeled, so "clean delivery never corrects" holds
## only after the link warms up. The authoring-tick
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
	p.client_prediction.stats.corrections = 0
	p.client_prediction.stats.max_replay_depth = 0
	p.server_prediction.stats.consumed = 0
	p.server_prediction.stats.missing = 0
	p.server_prediction.stats.starved = 0
	p.server_prediction.stats.held = 0
	p.server_prediction.stats.resync = 0
	p.server_prediction.stats.skipped = 0
	p.observer.reset()


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
	p.server_prediction.record_server_input(tick, _normalize(input))


## Runs one server consume step for [param p] at [param tick].
func consume_step(p: PredictedEntity, tick: int) -> void:
	p.server_prediction.simulate_tick(dt(), tick)


## Runs the server history recorder for [param p] at [param tick], the pass that
## normally follows a consume step, so a test can assert which timeline slot a
## consume actually wrote.
func record_server_history(p: PredictedEntity, tick: int) -> void:
	var record_tick := p.server_prediction.history_record_tick(tick)
	if record_tick < 0:
		return
	p.server_entity.timeline.record_state(
		record_tick,
		p.server_state.snapshot_payload(),
	)


## Returns the server's recorded authoritative state at [param tick].
func server_state_at(p: PredictedEntity, tick: int) -> Dictionary:
	if not p.server_entity or not p.server_entity.timeline:
		return { }
	return p.server_entity.timeline.latest_state_at_or_before(tick)


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
) -> LocalLinkConditions:
	var conditions := LocalLinkConditions.create(_seed)
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	conditions.latency_ms = float(delay_polls) * period
	conditions.jitter_ms = float(jitter_polls) * period
	conditions.packet_loss = loss
	return conditions
