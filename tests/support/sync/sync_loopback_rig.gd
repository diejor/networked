## Two-tree clocked loopback rig for the production [StampedSynchronizer] pair.
##
## Builds whatever synchronizer a factory returns, so the Phase 0 integration
## tests drive a real [StateSynchronizer] or [InputSynchronizer] through the same
## jitter-conditioned loopback that the spikes used to prove stamp coherence.
## Reuses [NetwTestHarness], [MultiplayerClock], and [LockstepStepper]. Manual
## node pairs (no [MultiplayerSpawner]) replicate between path-matched nodes.
class_name SyncLoopbackRig
extends RefCounted

var inner: NetwTestHarness
var client: MultiplayerTree
var server_clock: MultiplayerClock
var client_clock: MultiplayerClock

var server_node: Node2D
var client_node: Node2D
var server_sync: StampedSynchronizer
var client_sync: StampedSynchronizer

var _tree: SceneTree
var _tickrate: int
var _stepper: LockstepStepper


## Builds the rig: host, one client, clocks on both, and a matched node pair each
## carrying the synchronizer [param factory] returns.
func setup(
		suite: NetwTestSuite,
		factory: Callable,
		tickrate: int = 60,
		display_offset: int = 3,
) -> void:
	_tree = Engine.get_main_loop() as SceneTree
	_tickrate = tickrate
	inner = suite.make_harness()
	await inner.setup()
	client = await inner.add_client()
	server_clock = await inner.add_clock(tickrate, display_offset)
	client_clock = client.get_service(MultiplayerClock) as MultiplayerClock

	server_node = _build_node(factory)
	server_sync = server_node.get_node("Sync")
	inner.server().add_child(server_node)

	client_node = _build_node(factory)
	client_sync = client_node.get_node("Sync")
	client.add_child(client_node)

	await _tree.process_frame


## Advances both clocks by [param n] network ticks in-process, no real frames.
func sync_ticks(n: int) -> void:
	if _stepper == null:
		_stepper = LockstepStepper.new(
			[server_clock, client_clock] as Array[MultiplayerClock],
			[inner.server().multiplayer, client.multiplayer] as Array[MultiplayerAPI],
			inner.session(),
			_tickrate,
		)
	_stepper.sync_ticks(n)


## Installs an inbound delay (in polls) from the server onto the client.
func delay_server_to_client(
		delay_polls: int,
		seed: int = 1,
		jitter_polls: int = 0,
		loss: float = 0.0,
) -> void:
	var peer := client.multiplayer_peer as LocalMultiplayerPeer
	inner.session().set_link_conditions(
		peer,
		_conditions(delay_polls, seed, jitter_polls, loss),
		1,
	)


## Installs an inbound delay (in polls) from the client onto the server.
func delay_client_to_server(
		delay_polls: int,
		_seed: int = 2,
		jitter_polls: int = 0,
		loss: float = 0.0,
) -> void:
	var server_peer := inner.server().multiplayer_peer as LocalMultiplayerPeer
	inner.session().set_link_conditions(
		server_peer,
		_conditions(delay_polls, _seed, jitter_polls, loss),
		client.multiplayer_peer.get_unique_id(),
	)


func _conditions(
		delay_polls: int,
		seed: int,
		jitter_polls: int,
		loss: float,
) -> LocalLoopbackSession.LinkConditions:
	var conditions := LocalLoopbackSession.LinkConditions.new(seed)
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	conditions.latency_ms = float(delay_polls) * period
	conditions.jitter_ms = float(jitter_polls) * period
	conditions.packet_loss = loss
	return conditions


func _build_node(factory: Callable) -> Node2D:
	var node := Node2D.new()
	node.name = "SyncPlayer"
	var sync := factory.call() as StampedSynchronizer
	sync.name = "Sync"
	node.add_child(sync)
	sync.owner = node
	sync.root_path = sync.get_path_to(node)
	return node
