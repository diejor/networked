## Two-tree clocked loopback rig for a [method Netw.configure_property] derived-set
## node pair.
##
## The node carries no synchronizer: its script marks one state field and one
## input field, so the pipeline registers a derived binding on each peer through
## the node's own [signal Node.tree_entered]. The server adopts the node to mint a
## route, the [constant NetwSpawnBook.Recipe.ADOPT] spawn binds the path-matched
## client node, and from then on the state field flows server to client and the
## input field client to server over the shared
## [constant NetwFrameEnvelope.Channel.SYNC] frame.
class_name DerivedLoopbackRig
extends RefCounted

const PLAYER := preload("res://tests/support/sync/derived_state_player.gd")

var inner: NetwTestHarness
var client: MultiplayerTree
var server_clock: NetwClockInterface
var client_clock: NetwClockInterface

var server_node: Node2D
var client_node: Node2D

# A second client that neither authors nor controls the player. Present only when
# setup is asked for an observer, it proves the input set's server-only audience:
# it sees the public state row and never the input row.
var observer: MultiplayerTree
var observer_node: Node2D

var _tree: SceneTree
var _tickrate: int
var _stepper: LockstepStepper


## Builds the rig: host, one client, clocks on both, and a path-matched derived
## node pair. The server node is adopted (server authors state) and controlled by
## the client (client authors input). Pass [param with_observer] to add a second,
## uninvolved client for the audience assertion. Pass [param managed] false to
## own teardown explicitly (a case that runs the rig more than once).
func setup(
		suite: NetwTestSuite,
		tickrate: int = 60,
		with_observer: bool = false,
		managed: bool = true,
) -> void:
	_tree = Engine.get_main_loop() as SceneTree
	_tickrate = tickrate
	inner = suite.make_harness() if managed else suite.make_unmanaged_harness()
	await inner.setup()
	client = await inner.add_client()
	server_clock = await inner.add_clock(tickrate, 3)
	client_clock = client.api.clock

	server_node = _build_node()
	# A bound entity_id declares a real entity, so the rig activates LIVE rather
	# than staying an inert unbound node, keeping its derived state/input sync.
	NetwEntity.ensure(server_node).entity_id = &"DerivedPlayer"
	inner.server().add_child(server_node)
	client_node = _build_node()
	NetwEntity.ensure(client_node).entity_id = &"DerivedPlayer"
	client.add_child(client_node)

	if with_observer:
		observer = await inner.add_client()
		observer_node = _build_node()
		NetwEntity.ensure(observer_node).entity_id = &"DerivedPlayer"
		observer.add_child(observer_node)

	await _tree.process_frame

	# Adopt on the server mints the route and issues an ADOPT spawn that binds the
	# path-matched client nodes.
	inner.server().api.replication.adopt_in_place(server_node)
	sync_ticks(3)

	# The player is server-authored (node authority stays the server) and
	# client-controlled (its input is controller-authored). SMELL(authority-pin):
	# recording the controller after arm keeps node authority decoupled from it,
	# the decoupled case route-keyed authorship settles later.
	var cpid := client.multiplayer_peer.get_unique_id()
	NetwEntity.of(server_node).controller = cpid
	var client_entity := NetwEntity.of(client_node)
	if client_entity:
		client_entity.controller = cpid
	if observer_node:
		var observer_entity := NetwEntity.of(observer_node)
		if observer_entity:
			observer_entity.controller = cpid


func _build_node() -> Node2D:
	var node := PLAYER.new()
	node.name = "DerivedPlayer"
	return node


## Installs an inbound delay (in polls) from the server onto the client, the
## state stream's direction.
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
	inner.session().set_link_conditions(peer, conditions, 1)


## Advances every clock by [param n] network ticks in-process, no real frames.
func sync_ticks(n: int) -> void:
	if _stepper == null:
		var clocks: Array[NetwClockInterface] = [server_clock, client_clock]
		var apis: Array[MultiplayerAPI] = [inner.server().multiplayer, client.multiplayer]
		if observer:
			clocks.append(observer.api.clock)
			apis.append(observer.multiplayer)
		_stepper = LockstepStepper.new(clocks, apis, inner.session(), _tickrate)
	_stepper.sync_ticks(n)


## Tears the underlying harness down.
func teardown() -> void:
	await inner.teardown()
	_stepper = null
	inner = null
	client = null
	server_clock = null
	client_clock = null
	server_node = null
	client_node = null
	_tree = null
