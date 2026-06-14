## Two-tree clocked loopback rig for the lag-comp spike tiers A, C, D, E.
##
## Wraps [NetwTestHarness] with a [MultiplayerClock] on both peers and a matched
## server/client node pair carrying a [SpikeStateSync]. Manual nodes (no
## [MultiplayerSpawner]) mirror [TickNetworkTestHarness], which proves
## [MultiplayerSynchronizer] replicates between path-matched nodes without a
## spawner. Latency is installed per direction through [method degrade] /
## [method link_plan] so tests stay deterministic.
class_name SpikeNetHarness
extends RefCounted

var inner: NetwTestHarness
var client: MultiplayerTree
var server_clock: MultiplayerClock
var client_clock: MultiplayerClock

var server_node: Node2D
var client_node: Node2D
var server_sync: SpikeStateSync
var client_sync: SpikeStateSync

var _tree: SceneTree
var _tickrate: int
var _stepper: LockstepStepper


## Builds the rig: host, one client, clocks on both, and the matched node pair.
func setup(
		suite: NetwTestSuite,
		tickrate: int = 30,
		display_offset: int = 3,
		bundled: bool = false,
		managed: bool = true,
		delta: bool = false,
) -> void:
	_tree = Engine.get_main_loop() as SceneTree
	_tickrate = tickrate
	inner = suite.make_harness() if managed else suite.make_unmanaged_harness()
	await inner.setup()
	client = await inner.add_client()
	server_clock = await inner.add_clock(tickrate, display_offset)
	client_clock = client.get_service(MultiplayerClock) as MultiplayerClock

	server_node = _build_node(bundled, delta)
	server_sync = server_node.get_node("StateSync")
	inner.server().add_child(server_node)

	client_node = _build_node(bundled, delta)
	client_sync = client_node.get_node("StateSync")
	client_sync.write_through = true
	client.add_child(client_node)

	await _tree.process_frame


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


## Installs an exact inbound delay (in polls) from the server onto the client.
##
## Set [param jitter] / [param loss] for the impairment regime; leave them zero
## for the exact regime where convergence assertions use [code]==[/code].
func delay_server_to_client(
		delay_polls: int,
		_seed: int = 1,
		jitter_polls: int = 0,
		loss: float = 0.0,
) -> void:
	var peer := client.multiplayer_peer as LocalMultiplayerPeer
	var conditions := LocalLoopbackSession.LinkConditions.new(_seed)
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	conditions.latency_ms = float(delay_polls) * period
	conditions.jitter_ms = float(jitter_polls) * period
	conditions.packet_loss = loss
	# Inbound on the client keyed by the server sender (peer id 1).
	inner.session().set_link_conditions(peer, conditions, 1)


## Installs an exact inbound delay (in polls) from the client onto the server.
func delay_client_to_server(
		delay_polls: int,
		_seed: int = 2,
		jitter_polls: int = 0,
		loss: float = 0.0,
) -> void:
	var server_peer := inner.server().multiplayer_peer as LocalMultiplayerPeer
	var conditions := LocalLoopbackSession.LinkConditions.new(_seed)
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	conditions.latency_ms = float(delay_polls) * period
	conditions.jitter_ms = float(jitter_polls) * period
	conditions.packet_loss = loss
	inner.session().set_link_conditions(server_peer, conditions, client.multiplayer_peer.get_unique_id())


func _build_node(bundled: bool, delta: bool = false) -> Node2D:
	var node := Node2D.new()
	node.name = "SpikePlayer"
	var sync := SpikeStateSync.new()
	sync.name = "StateSync"
	sync.bundled = bundled
	sync.delta_mode = delta
	node.add_child(sync)
	sync.owner = node
	sync.root_path = sync.get_path_to(node)
	return node
