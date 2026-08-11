## Flat prediction and lag compensation family laws.
class_name TestFlatPredictionLagcomp
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "FlatPredictionTree"
	var service := LagCompensation.new()
	service.name = "LagCompensation"
	mt.add_child(service)
	add_child(mt)
	auto_free(mt)
	api = mt.api


func test_prediction_declaration_and_parameters_are_flat() -> void:
	var root := make_test_entity(mt, "PredictedSubject", 0, false)
	var entity := api.rid_of(root)

	assert_int(api.predict_declare(entity)).is_equal(OK)
	api.predict_set_param(
		entity,
		NetwMultiplayer.PredictParam.PREDICT_PARAM_SCHEDULE,
		NetwPredict.Schedule.FRAME,
	)
	assert_int(
		api.predict_get_param(
			entity,
			NetwMultiplayer.PredictParam.PREDICT_PARAM_SCHEDULE,
		),
	).is_equal(NetwPredict.Schedule.FRAME)
	assert_object(api._lagcomp.engine_for(NetwEntity.of(root))) \
			.is_not_null()

	api.predict_undeclare(entity)
	assert_object(api._lagcomp.engine_for(NetwEntity.of(root))) \
			.is_null()


func test_prediction_callbacks_and_island_are_rid_addressed() -> void:
	var root := make_test_entity(mt, "IslandOwner", 0, false)
	var other_root := make_test_entity(mt, "IslandMember", 0, false)
	var entity := api.rid_of(root)
	var other := api.rid_of(other_root)
	var sensor := func() -> int: return 17
	var simulate := func(_delta: float, _tick: int, _fresh: bool) -> void: pass

	api.predict_set_sensor_callback(entity, &"ground", sensor)
	api.predict_set_simulate_callback(entity, simulate)
	assert_int(api.predict_island_add(entity, other)).is_equal(OK)
	api.predict_island_set_param(
		entity,
		NetwMultiplayer.IslandParam.ISLAND_PARAM_APPROXIMATE,
		true,
	)

	var handle := NetwEntity.of(root).prediction
	assert_bool(handle.sensors[&"ground"] == sensor).is_true()
	assert_bool(handle.simulate == simulate).is_true()
	assert_bool(handle.island.participants.has(NetwEntity.of(other_root))) \
			.is_true()
	assert_bool(handle.island.approximate).is_true()


func test_timeline_and_lagcomp_queries_are_flat() -> void:
	var root := make_test_entity(mt, "HistorySubject", 0, false)
	var entity := api.rid_of(root)

	assert_int(api.timeline_declare(entity)).is_equal(OK)
	assert_object(api.timeline_sample(entity, 3)).is_not_null()
	assert_object(api.lagcomp_sample(entity, 3)).is_not_null()
	assert_object(api.lagcomp_action(func() -> bool: return true)) \
			.is_not_null()
	api.lagcomp_rewind([entity] as Array[RID], 3, func() -> void: pass)
	api.timeline_undeclare(entity)
