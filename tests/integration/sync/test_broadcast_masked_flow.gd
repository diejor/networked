## Client-authored masked broadcast over a clocked loopback pair, the display
## stream a controller fans out to observers that records into no timeline.
##
## A broadcast author is the first derived stream whose recipient sends it nothing
## back, so its per-recipient baseline can only promote on the standalone transport
## ack a silent observer emits at end-of-tick. Without that echo every masked frame
## stays a full row forever. This suite drives one client-authored
## [BroadcastAimBody] to a silent server observer and asserts the whole loop: the
## masked frames flow, the server's standalone ack promotes the author's baseline,
## the stream converges, no merge ever tears, and the observer records no rewind
## timeline for a trusted stream.
class_name TestBroadcastMaskedFlow
extends NetwTestSuite

const TICKRATE := 30

var harness: NetwTestHarness
var server: MultiplayerTree
var client: MultiplayerTree
var server_clock: NetwClockInterface
var client_clock: NetwClockInterface
var server_root: Node2D
var client_root: Node2D
var _stepper: LockstepStepper


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	harness = null
	_stepper = null
	server = null
	client = null
	server_clock = null
	client_clock = null
	server_root = null
	client_root = null
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


# Builds the clocked pair and one matched broadcast entity, route-bound on both
# peers, authored by the client toward the server observer.
func _setup_pair() -> void:
	harness = make_harness()
	await harness.setup()
	client = await harness.add_client()
	server = harness.server()
	server_clock = await harness.add_clock(TICKRATE)
	client_clock = client.api.clock
	server_clock.manual_tick = true
	client_clock.manual_tick = true

	var builder := PlayerBuilder.new("BroadcastProbe") \
			.with_root(BroadcastAimBody) \
			.with_broadcast([&"aim_dir"])
	server_root = builder.build()
	client_root = builder.build()
	server.add_child(server_root)
	client.add_child(client_root)

	var server_entity := NetwEntity.of(server_root)
	var client_entity := NetwEntity.of(client_root)
	NetwBench.bind_shared_route(server_root, [client_root])
	# The client steers the entity, so only it authors the broadcast and the server
	# is a pure observer that sends the author nothing to piggyback an echo on.
	var client_id := client.api.get_unique_id()
	server_entity.controller = client_id
	client_entity.controller = client_id
	await get_tree().process_frame

	_stepper = LockstepStepper.new(
		[server_clock, client_clock] as Array[NetwClockInterface],
		[server.multiplayer, client.multiplayer] as Array[MultiplayerAPI],
		harness.session(),
		TICKRATE,
	)


# Impairs the client-to-server path so the author's masked frames drop, reorder,
# and duplicate, all deterministic under the seed.
func _impair_uplink(seed_value: int) -> void:
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	var conditions := LocalLoopbackSession.LinkConditions.new(seed_value)
	conditions.latency_ms = 2.0 * period
	conditions.jitter_ms = 6.0 * period
	conditions.reorder = 1.0
	conditions.duplicate = 0.3
	conditions.packet_loss = 0.05
	harness.session().set_link_conditions(
		server.multiplayer_peer as LocalMultiplayerPeer,
		conditions,
		client.api.get_unique_id(),
	)


func test_client_broadcast_promotes_via_standalone_echo_and_heals_under_loss() -> void:
	await _setup_pair()

	var client_binding := NetwEntity.of(client_root).broadcast_binding
	assert_that(client_binding).is_not_null()
	var last_tick := [-1]
	var author := func(_d: float, t: int) -> void:
		client_binding.authored_tick = t
		client_root.aim_dir = Vector2(t, -t)
		last_tick[0] = t
	client_clock.on_tick.connect(author)

	# The server observer merges the masked stream without ever tearing a row.
	var torn := [0]
	NetwEntity.of(server_root).broadcast_binding.on_applied = (
			func(header: Dictionary) -> void:
				var tick := int(header.get("tick", -1))
				if tick < 0:
					return
				var payload: Dictionary = header.get("payload", { })
				if not (payload.get(&"aim_dir", Vector2.ZERO) as Vector2) \
						.is_equal_approx(Vector2(tick, -tick)):
					torn[0] += 1
	)

	# Clean phase: the author's masked frames land, and the silent server's
	# standalone ack promotes the author's per-recipient baseline.
	_stepper.sync_ticks(40)

	var client_snap := client.api.monitor_snapshot()
	var server_snap := server.api.monitor_snapshot()
	assert_int(int(client_snap[&"masked_frames_out"])).is_greater(0)
	assert_int(int(server_snap[&"derived_frames_in"])).is_greater(0)

	# The server authors nothing toward the client, so every ack it sends is a
	# standalone echo. That echo is the only thing that advances the client's
	# confirmed baseline seq for the server.
	assert_int(int(server_snap[&"standalone_acks_out"])).is_greater(0)
	assert_int(client.api.peer_state_ack(1)).is_greater_equal(0)

	# A broadcast records into no rewind timeline, the boundary as a test: the
	# observer holds the binding but the entity never grows a timeline.
	assert_that(NetwEntity.of(server_root).broadcast_binding).is_not_null()
	assert_that(NetwEntity.of(server_root).timeline).is_null()

	# Loss phase: dropped frames and lost acks must never let a merge produce a
	# value the author never held.
	_impair_uplink(53)
	_stepper.sync_ticks(80)
	assert_int(torn[0]).is_equal(0)

	# Heal phase: once the link recovers the baseline catches up and the observer
	# converges on the exact final authored value.
	client_clock.on_tick.disconnect(author)
	harness.clear_links()
	_stepper.sync_ticks(6)
	var t: int = last_tick[0]
	assert_that(server_root.aim_dir).is_equal(Vector2(t, -t))
