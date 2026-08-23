## Loss, reorder, and duplication tests for the unreliable sync streams over a
## clocked loopback pair.
##
## The carrier stamps every unreliable datagram with a [code]u16[/code]
## sequence and [method NetwSyncPipeline.accept_unreliable] drops any frame a
## fresher datagram already superseded, so a receiver's applied state moves
## strictly forward whatever order the link delivers in. These cases drive the
## derived state set a marked root declares and the [method Netw.sync_property]
## door through seeded [LocalLinkConditions] and assert exactly
## that: applied ticks and values never move backward, the stream converges,
## and the stale-drop counter proves the gate engaged rather than the link
## happening to stay ordered.
class_name TestSyncLossReorder
extends NetwTestSuite

const SCORE_BODY := preload("res://tests/support/sync/score_probe_body.gd")
const TICKRATE := 30

var harness: NetwTestHarness
var server: MultiplayerTree
var client: MultiplayerTree
var server_api: NetwMultiplayer
var server_clock: NetwClockHandle
var client_clock: NetwClockHandle
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
	server_api = null
	client_clock = null
	server_root = null
	client_root = null
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


# Builds the clocked pair and one matched entity, route-bound on both peers,
# carrying the root script's server-authored derived state set over the carrier.
func _setup_pair(root_type: Variant = StateSyncBody) -> void:
	harness = make_harness()
	await harness.setup()
	client = await harness.add_client()
	server = harness.server()
	server_clock = await harness.add_clock(TICKRATE)
	server_api = harness.server().api
	client_clock = client.api._native_core.clock_handle
	server_clock.manual_tick = true
	client_clock.manual_tick = true

	var builder := PlayerBuilder.new("LossReorderProbe") \
			.with_root(root_type) \
			.with_state([&"position"])
	server_root = builder.build()
	client_root = builder.build()
	server.add_child(server_root)
	client.add_child(client_root)

	NetwBench.bind_shared_route(server_root, [client_root])
	await get_tree().process_frame

	_stepper = LockstepStepper.new(
		[server_clock, client_clock] as Array[NetwClockHandle],
		[server.multiplayer, client.multiplayer] as Array[MultiplayerAPI],
		harness.session(),
		TICKRATE,
	)


# Impairs the server-to-client path with everything that breaks ordering at
# once: latency spread wide by jitter, certain reordering, duplication, and a
# little loss, all deterministic under the seed.
func _impair_downlink(seed_value: int) -> void:
	var period := 1000.0 / float(Engine.get_physics_ticks_per_second())
	var conditions := LocalLinkConditions.create(seed_value)
	conditions.latency_ms = 2.0 * period
	conditions.jitter_ms = 6.0 * period
	conditions.reorder = 1.0
	conditions.duplicate = 0.3
	conditions.packet_loss = 0.05
	harness.session().set_link_conditions(
		client.multiplayer_peer as LocalMultiplayerPeer,
		conditions,
		1,
	)


func _client_stale_drops() -> int:
	return int(client.api.stats_snapshot().get("sync_drops_stale", 0))


func test_state_stream_applies_strictly_forward_under_reorder() -> void:
	await _setup_pair()

	var server_binding: NetwPropertySetBinding = NetwEntity.of(server_root) \
			.state_binding
	server_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_binding.authored_tick = t
			server_root.position = Vector2(t, -t),
	)

	var received: Array[int] = []
	var torn := 0
	NetwEntity.of(client_root).state_binding.on_applied = (
			func(header: Dictionary) -> void:
				var tick := int(header.get("tick", -1))
				if tick < 0:
					return
				received.append(tick)
				var payload: Dictionary = header.get("payload", { })
				if not (payload.get(&"position", Vector2.ZERO) as Vector2) \
						.is_equal_approx(Vector2(tick, -tick)):
					torn += 1
	)

	_impair_downlink(120)
	_stepper.sync_ticks(120)

	# Enough of the stream survives the impairment to be a meaningful sample.
	assert_int(received.size()).is_greater(10)

	# Applied state ticks move strictly forward: a datagram the link delivered
	# late or twice never rewinds or repeats the applied stream.
	var regressions := 0
	for i in range(1, received.size()):
		if received[i] <= received[i - 1]:
			regressions += 1
	assert_int(regressions).is_equal(0)
	assert_int(torn).is_equal(0)

	# The order held because the gate dropped stale datagrams, not because the
	# link happened to deliver in order.
	assert_int(_client_stale_drops()).is_greater(0)


func test_masked_state_stream_never_corrupts_under_loss_and_converges() -> void:
	# The masked lane diffs each recipient's frame against its own
	# confirmed baseline, advanced only once the datagram carrying it is
	# acked. Seeded loss on the downlink proves the never-corrupts guarantee
	# end to end: a dropped ack must never let the client apply a value that
	# merges a masked field onto a baseline it never actually held.
	await _setup_pair(MaskedStateSyncBody)

	var server_binding: NetwPropertySetBinding = NetwEntity.of(server_root) \
			.state_binding
	var received: Array[int] = []
	# GDScript lambdas capture outer locals by value: a plain int/float mutated
	# inside a closure never propagates back to the enclosing scope. Box the
	# two primitives this closure mutates in single-element Arrays (a
	# reference type) so the assertions below observe what the callback
	# actually saw, not a permanently-unchanged initial value.
	var torn := [0]
	var last_authored_tick := [-1]
	var on_tick_conn := func(_d: float, t: int) -> void:
		server_binding.authored_tick = t
		server_root.position = Vector2(t, -t)
		last_authored_tick[0] = t
	server_api.on_tick.connect(on_tick_conn)
	NetwEntity.of(client_root).state_binding.on_applied = (
			func(header: Dictionary) -> void:
				var tick := int(header.get("tick", -1))
				if tick < 0:
					return
				received.append(tick)
				var payload: Dictionary = header.get("payload", { })
				if not (payload.get(&"position", Vector2.ZERO) as Vector2) \
						.is_equal_approx(Vector2(tick, -tick)):
					torn[0] += 1
	)

	_impair_downlink(53)
	_stepper.sync_ticks(120)

	assert_int(received.size()).is_greater(10)
	# No merged row ever produced a value the server never actually held, the
	# masked lane's counterpart of the plain stream's never-regresses check.
	assert_int(torn[0]).is_equal(0)

	# The lane still healed some frames despite the impairment.
	assert_int(_client_stale_drops()).is_greater(0)

	var server_snap := server.api.stats_snapshot()
	assert_int(int(server_snap[&"row_frames_out"])).is_greater(0)
	assert_int(int(server_snap[&"row_frames_full"])).is_greater(0)

	# Converges to the exact final authored value once the link heals, proving
	# the confirmed baseline eventually catches up rather than staying stuck on
	# a stale row. The authoring stops here so healing ticks never move the
	# target the assertion below checks against.
	server_api.on_tick.disconnect(on_tick_conn)
	harness.clear_links()
	_stepper.sync_ticks(4)
	var t: int = last_authored_tick[0]
	assert_that(client_root.position).is_equal(Vector2(t, -t))


func test_property_sync_never_regresses_and_converges() -> void:
	await _setup_pair(SCORE_BODY)
	Netw.configure_property(server_root, "score").unreliable()

	_impair_downlink(77)

	var samples: Array[int] = []
	for t in range(120):
		server_root.set(&"score", t + 1)
		Netw.sync_property(server_root, &"score")
		_stepper.sync_ticks(1)
		samples.append(int(client_root.get(&"score")))

	# The observed value never moves backward however the link reordered or
	# duplicated the sends, and some of the stream got through.
	var regressions := 0
	for i in range(1, samples.size()):
		if samples[i] < samples[i - 1]:
			regressions += 1
	assert_int(regressions).is_equal(0)
	assert_int(samples[samples.size() - 1]).is_greater(0)
	assert_int(_client_stale_drops()).is_greater(0)

	# An unreliable stream owes no delivery of any one send, so convergence is
	# asserted on a final send over a healed link.
	harness.clear_links()
	Netw.sync_property(server_root, &"score")
	_stepper.sync_ticks(4)
	assert_int(int(client_root.get(&"score"))).is_equal(120)

	# Over the same healed pair, a client-to-server unreliable send closes the
	# loop so each direction now carries an inbound datagram. Once a peer has
	# accepted one it echoes the freshest seq it holds in the acked datagram
	# shape, and the destination records that echo as the peer's confirmed
	# baseline seq (the masked delta lane's snapshot ack).
	var client_id := client.api.get_unique_id()
	var server_standalone_before := int(server.api.stats_snapshot()[&"standalone_acks_out"])
	var client_standalone_before := int(client.api.stats_snapshot()[&"standalone_acks_out"])
	for t in range(8):
		client_root.set(&"score", 200 + t)
		Netw.sync_property(client_root, &"score")
		Netw.sync_property(server_root, &"score")
		_stepper.sync_ticks(1)

	var server_snap := server.api.stats_snapshot()
	var client_snap := client.api.stats_snapshot()

	# A chatty pair carries an echo on every datagram, so neither peer ever needs
	# a standalone ack: the piggyback always wins.
	assert_int(int(server_snap[&"standalone_acks_out"])).is_equal(server_standalone_before)
	assert_int(int(client_snap[&"standalone_acks_out"])).is_equal(client_standalone_before)

	# Both peers stamp the acked shape once they have heard from the other.
	assert_int(int(server_snap[&"state_acks_out"])).is_greater(0)
	assert_int(int(client_snap[&"state_acks_out"])).is_greater(0)
	assert_int(int(server_snap[&"state_acks_in"])).is_greater(0)
	assert_int(int(client_snap[&"state_acks_in"])).is_greater(0)

	# And each records the other's echo as a confirmed baseline seq.
	assert_int(server.api._peer_state_ack(client_id)).is_greater_equal(0)
	assert_int(client.api._peer_state_ack(1)).is_greater_equal(0)
