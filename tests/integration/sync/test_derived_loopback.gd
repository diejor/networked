## Integration tests for a [method Netw.configure_property] derived-set node
## through a clocked loopback pair, the first live proof of the derived sync flip.
##
## A node marks one state field and one input field, with no synchronizer. The
## state field rides the shared [constant NetwFrameEnvelope.Channel.SYNC] frame
## from the server (its authority) to every peer, and the input field rides the
## same frame from the controlling client to the server alone, its
## [constant NetwSyncSet.Audience.AUDIENCE_SERVER_ONLY] reach. The ordinal split
## and the spawn-time schema descriptor let both peers resolve a bare
## [code](route, ordinal)[/code] to the right node and set.
class_name TestDerivedLoopback
extends NetwTestSuite

var rig: DerivedLoopbackRig


func test_state_flows_to_client_and_input_flows_to_server() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)

	# The ADOPT spawn bound a route on the client and its derived descriptors
	# validated, so the client registered its own binding.
	assert_object(NetwEntity.of(rig.client_node)).is_not_null()

	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.server_node.position = Vector2(t, -t))
	rig.client_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.client_node.rotation = float(t) * 0.01)

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
	assert_int(rig.client.api.monitor_snapshot()[&"derived_frames_in"]).is_greater(0)
	assert_int(rig.inner.server().api.monitor_snapshot()[&"derived_frames_in"]).is_greater(0)


func test_entity_resolves_derived_set_handles() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)

	# The registry set handles resolve through the session, the replacement for the
	# set handles a prediction engine reads.
	var entity := NetwEntity.of(rig.server_node)
	assert_object(entity.state_binding).is_not_null()
	assert_object(entity.input_binding).is_not_null()
	assert_int(entity.state_binding.set.record).is_equal(NetwSyncSet.Record.RECORD_STATE)
	assert_int(entity.input_binding.set.record).is_equal(NetwSyncSet.Record.RECORD_INPUT)
	assert_object(entity.state_binding.node()).is_same(rig.server_node)


func test_server_only_input_never_reaches_another_client() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self, 60, true)

	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.server_node.position = Vector2(t, -t))
	rig.client_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			rig.client_node.rotation = float(t) * 0.01)

	rig.sync_ticks(60)

	# The observer received the public state row.
	assert_float(rig.observer_node.position.x).is_greater(0.0)
	# The observer never received the server-only input row, so its rotation is
	# still the default while the server's tracks the controller.
	assert_float(rig.observer_node.rotation).is_equal(0.0)
	assert_float(rig.server_node.rotation).is_greater(0.0)
