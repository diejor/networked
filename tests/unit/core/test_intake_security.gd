## Security regressions for hostile carrier admission.
class_name TestIntakeSecurity
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer
var entity: NetwEntity
var calls: Array[int]


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "IntakeSecurity"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	var owner := Node2D.new()
	owner.name = "Predicted"
	mt.add_child(owner)
	auto_free(owner)
	entity = NetwEntity.ensure(owner)
	entity.controller = 2
	api._liveness.bind_route(7, entity)
	calls = []


func test_predict_command_rejects_a_non_controller_sender() -> void:
	api._replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_COMMAND,
		func(_entity: NetwEntity, _payload: PackedByteArray, _sender: int) -> void:
			calls.append(_sender),
	)

	api._drive_carrier(
		3,
		NetwFrameEnvelope.pack(
			7,
			0,
			NetwFrameEnvelope.Channel.PREDICT_COMMAND,
			PackedByteArray([1]),
		),
		true,
	)

	assert_array(calls).is_empty()


func test_predict_ack_rejects_a_non_server_sender() -> void:
	api._replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_ACK,
		func(_entity: NetwEntity, _payload: PackedByteArray, _sender: int) -> void:
			calls.append(_sender),
	)

	api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			7,
			0,
			NetwFrameEnvelope.Channel.PREDICT_ACK,
			PackedByteArray([1]),
		),
		true,
	)

	assert_array(calls).is_empty()


func test_gate_verdict_preserves_the_legacy_drop_counter() -> void:
	api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			99,
			0,
			NetwFrameEnvelope.Channel.SYNC,
			PackedByteArray([0, 0]),
		),
		true,
	)

	var snapshot := api.stats_snapshot()
	assert_int(snapshot[&"drops_unknown_route"]).is_equal(1)
	assert_int(snapshot[&"verdict_does_not_exist"]).is_equal(1)


func test_direct_tick_door_services_the_session() -> void:
	assert_int(api._drive_tick(7)).is_equal(OK)


func test_gate_verdicts_are_partitioned_and_counted() -> void:
	assert_int(
		_finish_gate(
			api._sync_admit_frame(
				2,
				99,
				0,
				NetwFrameEnvelope.Channel.SYNC,
				0,
				-1,
				PackedByteArray([1]),
			),
			99,
		),
	).is_equal(ERR_DOES_NOT_EXIST)
	assert_int(
		api.get_stat(
			NetwMultiplayer.Stat.STAT_VERDICT_DOES_NOT_EXIST,
		),
	).is_equal(1)

	api._liveness.core.set_state(
		entity.rid,
		NetwLivenessCore.STATE_DEAD,
	)
	assert_int(
		_finish_gate(
			api._sync_admit_frame(
				2,
				7,
				0,
				NetwFrameEnvelope.Channel.SYNC,
				0,
				-1,
				PackedByteArray([1]),
			),
			7,
		),
	).is_equal(ERR_SKIP)
	assert_int(
		api.get_stat(NetwMultiplayer.Stat.STAT_VERDICT_SKIP),
	).is_equal(1)

	var unavailable_owner := Node2D.new()
	unavailable_owner.name = "Unavailable"
	mt.add_child(unavailable_owner)
	auto_free(unavailable_owner)
	var unavailable_entity := NetwEntity.ensure(unavailable_owner)
	api._liveness.bind_route(8, unavailable_entity)
	unavailable_entity.owner = null
	assert_int(
		_finish_gate(
			api._sync_admit_frame(
				2,
				8,
				0,
				NetwFrameEnvelope.Channel.SYNC,
				0,
				-1,
				PackedByteArray([1]),
			),
			8,
		),
	).is_equal(ERR_UNAVAILABLE)
	assert_int(
		api.get_stat(
			NetwMultiplayer.Stat.STAT_VERDICT_UNAVAILABLE,
		),
	).is_equal(1)
	unavailable_entity.owner = unavailable_owner

	assert_int(
		_finish_gate(
			api._spawn_admit_frame(
				2,
				0,
				NetwFrameEnvelope.Channel.SPAWN,
				PackedByteArray([1]),
			),
			0,
		),
	).is_equal(ERR_UNAUTHORIZED)
	assert_int(
		api.get_stat(
			NetwMultiplayer.Stat.STAT_VERDICT_UNAUTHORIZED,
		),
	).is_equal(1)

	assert_int(
		_finish_gate(
			api._spawn_admit_frame(
				1,
				0,
				NetwFrameEnvelope.Channel.SPAWN,
				PackedByteArray(),
			),
			0,
		),
	).is_equal(ERR_INVALID_DATA)
	assert_int(
		api.get_stat(
			NetwMultiplayer.Stat.STAT_VERDICT_INVALID_DATA,
		),
	).is_equal(1)


# Applies the same verdict sink used by the production carrier roots.
func _finish_gate(verdict: Error, route: int) -> Error:
	return api._finish_gate_verdict(verdict, route)
