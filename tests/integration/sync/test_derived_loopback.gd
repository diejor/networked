## Integration tests for a [method Netw.configure_property] derived-set node
## through a clocked loopback pair, the first live proof of the derived sync flip.
##
## A node marks one state field and one input field, with no synchronizer. The
## state field rides the shared [constant NetwFrameEnvelope.Channel.SYNC] frame
## from the server (its authority) to every peer, and the input field rides the
## same frame from the controlling client to the server alone, its
## [constant NetwPropertySet.Audience.AUDIENCE_SERVER_ONLY] reach. The ordinal split
## and the spawn-time schema descriptor let both peers resolve a bare
## [code](route, ordinal)[/code] to the right node and set.
class_name TestDerivedLoopback
extends NetwTestSuite

const CONTROLLER_STATE_PLAYER := preload(
	"res://tests/support/sync/controller_state_player.gd"
)

var rig: DerivedLoopbackRig


func test_state_flows_to_client_and_input_flows_to_server() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)

	# The ADOPT spawn bound a route on the client and its derived descriptors
	# validated, so the client registered its own binding.
	assert_object(NetwEntity.of(rig.client_node)).is_not_null()

	rig.server_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.server_node.position = Vector2(t, -t)
	)
	rig.client_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.client_node.rotation = float(t) * 0.01
	)

	rig.sync_ticks(60)

	# State: the client tracks the server's authored position within a few ticks
	# of in-flight lag, and never runs ahead of the authority.
	assert_float(rig.client_node.position.x).is_greater(0.0)
	assert_float(rig.client_node.position.x).is_less_equal(rig.server_node.position.x)
	assert_float(rig.server_node.position.x - rig.client_node.position.x).is_less(6.0)

	# Input: the server tracks the client's authored rotation the same way.
	assert_float(rig.server_node.rotation).is_greater(0.0)
	assert_float(rig.server_node.rotation).is_less_equal(rig.client_node.rotation)
	assert_float(rig.client_node.rotation - rig.server_node.rotation).is_less(0.06)

	# Both peers actually applied derived frames, not merely coincident defaults.
	assert_int(rig.client.api.stats_snapshot()[&"derived_frames_in"]).is_greater(0)
	assert_int(rig.inner.server().api.stats_snapshot()[&"derived_frames_in"]).is_greater(0)


## Verify a sync pass leaves one row for the pass and one for every peer it
## encoded to, and that a peer applying those bytes leaves the decode's own row
## carrying the stage's verdict. Encode and gather are two facts rather than
## one: a pass that gathered offers and sent to nobody is exactly what a silent
## stream looks like from outside.
func test_a_sync_pass_reports_the_stages_that_carried_it() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)
	var server := rig.inner.server().api
	server._native_core.event_arm(true)
	rig.client.api._native_core.event_arm(true)

	rig.server_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.server_node.position = Vector2(t, -t)
	)
	rig.sync_ticks(20)

	var sent := _stage_counts(server, 0)
	assert_int(int(sent.get(NetwMultiplayerCore.GATHER, 0))).is_greater(0)
	assert_int(int(sent.get(NetwMultiplayerCore.SYNC_ENCODE, 0))).is_greater(0)

	var route := NetwEntity.of(rig.client_node).route
	var applied := _stage_counts(rig.client.api, route)
	assert_int(int(applied.get(NetwMultiplayerCore.SYNC_DECODE, 0))).is_greater(0)

	# The receive pass is the mirror of the send pass: a datagram that carried
	# nothing and one whose every frame was refused read the same from the
	# per-frame rows alone, and only the pass row separates them.
	var received := _stage_counts(rig.client.api, 0)
	assert_int(int(received.get(NetwMultiplayerCore.APPLY, 0))).is_greater(0)


# How many rows of each kind a session recorded on a route. Reading a ring
# empties it, so each route is drained exactly once.
func _stage_counts(api: NetwMultiplayer, route: int) -> Dictionary:
	var seen: Dictionary[int, int] = { }
	for row: NetwEvent in api._native_core.event_ring(route):
		seen[row.event] = int(seen.get(row.event, 0)) + 1
	return seen


func test_entity_resolves_derived_set_handles() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)

	# The registry set handles resolve through the session, the replacement for the
	# set handles a prediction engine reads.
	var entity := NetwEntity.of(rig.server_node)
	assert_object(entity.state_binding).is_not_null()
	assert_object(entity.input_binding).is_not_null()
	assert_int(entity.state_binding.set.record).is_equal(NetwPropertySet.Record.RECORD_STATE)
	assert_int(entity.input_binding.set.record).is_equal(NetwPropertySet.Record.RECORD_INPUT)
	assert_object(entity.state_binding.node()).is_same(rig.server_node)


func test_server_only_input_never_reaches_another_client() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self, 60, true)

	rig.server_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.server_node.position = Vector2(t, -t)
	)
	rig.client_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.client_node.rotation = float(t) * 0.01
	)

	rig.sync_ticks(60)

	# The observer received the public state row.
	assert_float(rig.observer_node.position.x).is_greater(0.0)
	# The observer never received the server-only input row, so its rotation is
	# still the default while the server's tracks the controller.
	assert_float(rig.observer_node.rotation).is_equal(0.0)
	assert_float(rig.server_node.rotation).is_greater(0.0)


func test_reports_controller_authored_public_state_relay() -> void:
	rig = DerivedLoopbackRig.new()
	rig.player_type = CONTROLLER_STATE_PLAYER
	await rig.setup(self, 60, true)

	rig.client_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.client_node.position = Vector2(t, -t)
	)
	rig.sync_ticks(60)

	print(
		"[relay] controller=%s server=%s observer=%s observed=%s" % [
			rig.client_node.position,
			rig.server_node.position,
			rig.observer_node.position,
			rig.observer_node.position != Vector2.ZERO,
		],
	)
	assert_bool(is_instance_valid(rig.observer_node)).is_true()


const RETAINED_STATE_PLAYER := preload(
	"res://tests/support/sync/retained_state_player.gd"
)


func test_a_retained_field_crosses_only_when_it_changes() -> void:
	rig = DerivedLoopbackRig.new()
	rig.player_type = RETAINED_STATE_PLAYER
	await rig.setup(self)

	rig.server_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.server_node.position = Vector2(t, -t)
	)
	rig.sync_ticks(20)

	var settled: int = rig.inner.server().api.stats_snapshot()[&"retained_frames_out"]
	assert_bool(rig.client_node.stunned).is_false()

	# The volatile half keeps sending every tick while the retained half sends
	# nothing, which is the whole reason the two lanes are separate.
	rig.sync_ticks(20)
	assert_int(rig.inner.server().api.stats_snapshot()[&"retained_frames_out"]) \
		.is_equal(settled)
	assert_float(rig.client_node.position.x).is_greater(0.0)

	rig.server_node.stunned = true
	rig.sync_ticks(10)

	assert_bool(rig.client_node.stunned).is_true()
	assert_int(rig.inner.server().api.stats_snapshot()[&"retained_frames_out"]) \
		.is_greater(settled)


func test_the_input_lane_repeats_recent_ticks_rather_than_retransmitting() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)

	rig.client_api.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.client_node.rotation = float(t) * 0.01
	)
	rig.sync_ticks(30)

	# The input set is windowed by declaration, so its frames ride the window
	# lane and each one carries more than the tick it was sent for. Equal counts
	# would mean the redundancy that heals a lost input was never bought.
	var stats: Dictionary = rig.client.api.stats_snapshot()
	assert_int(stats[&"window_frames_out"]).is_greater(0)
	assert_int(stats[&"window_samples_out"]).is_greater(stats[&"window_frames_out"])
	assert_float(rig.server_node.rotation).is_greater(0.0)
