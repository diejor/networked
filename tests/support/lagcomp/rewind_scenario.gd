## Server-only lag-comp rewind rig: host, a clock, a mounted [LagCompensation]
## node, and a [LockstepStepper].
##
## A server-authoritative state-set-only entity is recorded every tick,
## so it is rewindable by default and a scene rewind can read where a target was.
## This rig owns the server-only ritual the sim-integration suites used to
## hand-roll (host, add the clock, mount the [LagCompensation] node, drive the
## [LockstepStepper], spawn a rewindable entity) so a test reads as the rewind
## claim, not the setup. Sample and rewind access stays on
## [member NetwMultiplayer.lag_compensation].
##
## [codeblock]
## var r := RewindScenario.new()
## await r.setup(self)
## var e := await r.spawn_state_entity("Target")
## r.move_along(e, func(i: int) -> Vector2: return Vector2(i * 8.0, 0.0), 24)
## var view_tick := r.clock.tick - 8
## var handle := r.server.api.entity_of(e.owner)
## var past := r.server.api.lagcomp_sample(handle, tick)
## [/codeblock]
class_name RewindScenario
extends RefCounted

const TICKRATE := 30
const DISPLAY_OFFSET := 3

var inner: NetwTestHarness
var server: MultiplayerTree
var clock: NetwClockHandle
var sim: NetwMultiplayer

var _suite: NetwTestSuite
var _tree: SceneTree
var _stepper: LockstepStepper
var _tickrate: int


## Builds the scenario: a hosted server, a clock, the resolved service, and the
## stepper.
##
## Pass [code]managed = false[/code] to own teardown explicitly.
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
	await inner.host_server()
	server = inner.server()
	clock = await inner.add_clock(tickrate, display_offset)

	# The service is no longer auto-created, so mount the node explicitly.
	sim = inner.add_lag_compensation()
	await _tree.process_frame

	# Freeze the clock under lockstep so every tick is driven by run() / move_along().
	_stepper = LockstepStepper.new(
		[clock] as Array[NetwClockHandle],
		[server.multiplayer] as Array[MultiplayerAPI],
		inner.session(),
		tickrate,
	)


## Spawns a server-authoritative state-synced entity with no prediction.
##
## The [StateSyncBody] root declares a position state set alone, so timeline
## registration is driven by state-set presence, not a [PredictionComponent].
func spawn_state_entity(
		entity_name: String = "Target",
		props: Array[StringName] = [&"position"],
) -> NetwEntity:
	var node := PlayerBuilder.new(entity_name) \
			.with_root(StateSyncBody) \
			.with_state(props) \
			.build()
	# A bound entity_id declares a real entity, so the rig activates LIVE
	# (authority application, scene registration) rather than staying an inert
	# unbound node. This rig has no "|"-separated name to hydrate one from.
	NetwEntity.of(node).entity_id = StringName(entity_name)
	server.add_child(node)
	await _tree.process_frame
	return NetwEntity.of(node)


## Spawns a state-synced entity whose controller is player-represented, so it
## can be despawned with [member NetwDespawnOpts.linger].
func spawn_despawnable_entity(
		entity_name: String = "Linger",
		props: Array[StringName] = [&"position"],
) -> Node2D:
	var node := PlayerBuilder.new(entity_name) \
			.with_root(StateSyncBody) \
			.with_multiplayer_entity() \
			.with_state(props) \
			.build()
	server.add_child(node)
	await _tree.process_frame
	return node as Node2D


## Advances the server clock by [param n] ticks, recording state each tick.
func run(n: int) -> void:
	_stepper.sync_ticks(n)


## Drives [param entity]'s body along [param trajectory], one tick per step.
##
## [param trajectory] is a [code]func(i: int) -> Vector2[/code] returning the body
## position for step [code]i[/code], so each tick records a fresh authoritative
## position into the timeline. Returns the live position after the last step.
func move_along(
		entity: NetwEntity,
		trajectory: Callable,
		ticks: int,
) -> Vector2:
	var node := entity.owner as Node2D
	for i in range(ticks):
		node.position = trajectory.call(i)
		_stepper.sync_ticks(1)
	return node.position


## Tears the underlying harness down. Only needed for an unmanaged setup.
func teardown() -> void:
	await inner.teardown()
	_stepper = null
	inner = null
	server = null
	clock = null
	sim = null
	_suite = null
	_tree = null
