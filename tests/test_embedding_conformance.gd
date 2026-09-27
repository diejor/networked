class_name TestEmbeddingConformance
extends NetwTestSuite

const _LEVEL := preload("res://tests/support/declared_level.tscn")

var _original: MultiplayerAPI
var _active: NetwEmbeddingWorld


func before_test() -> void:
	_original = get_tree().get_multiplayer()


func after_test() -> void:
	if _active != null:
		_active.dispose()
		_active = null
	get_tree().set_multiplayer(_original)
	await drain_frames(get_tree(), 3)
	await super.after_test()

#region Provider builders

func _scoped() -> NetwScopedWorld:
	_active = NetwScopedWorld.new(self)
	return _active


func _root() -> NetwRootWorld:
	_active = NetwRootWorld.new(self)
	return _active

#endregion

#region Scenarios (provider-agnostic — never name a MultiplayerTree or install verb)

func _scenario_host_comes_online(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	assert_bool(host.is_online).override_failure_message(
		"[%s] host never came online through its own verb" % world.provider(),
	).is_true()
	assert_bool(host.is_host).override_failure_message(
		"[%s] online host does not report is_host" % world.provider(),
	).is_true()
	assert_int(host.get_unique_id()).override_failure_message(
		"[%s] host is not peer 1" % world.provider(),
	).is_equal(1)


func _scenario_client_is_admitted(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	var client := await world.add_client("p1")

	assert_object(client.local_player).override_failure_message(
		"[%s] client was never admitted to its own session" % world.provider(),
	).is_not_null()

	var client_id := client.get_unique_id()
	var seen := await world.pump_until(
		func() -> bool: return host.peer_get_player(client_id) != null
	)
	assert_bool(seen).override_failure_message(
		"[%s] host admitted no roster row for the client" % world.provider(),
	).is_true()


func _scenario_roster_crosses_both_ways(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	var c1 := await world.add_client("p1")
	var c2 := await world.add_client("p2")
	var id1 := c1.get_unique_id()
	var id2 := c2.get_unique_id()

	var host_sees_both := await world.pump_until(
		func() -> bool:
			return host.peer_get_player(id1) != null \
					and host.peer_get_player(id2) != null
	)
	assert_bool(host_sees_both).override_failure_message(
		"[%s] host roster is missing one of the two clients" % world.provider(),
	).is_true()

	var clients_see_each_other := await world.pump_until(
		func() -> bool:
			return c1.peer_get_player(id2) != null \
					and c2.peer_get_player(id1) != null
	)
	assert_bool(clients_see_each_other).override_failure_message(
		"[%s] client rosters did not cross (each should see the other)"
		% world.provider(),
	).is_true()


func _scenario_spawn_reaches_every_peer(world: NetwEmbeddingWorld) -> void:
	var _host := await world.host()
	var c1 := await world.add_client("p1")
	var c2 := await world.add_client("p2")

	var route := await world.spawn_probe("probe")

	var c1_live := await world.pump_until(
		func() -> bool:
			return c1.liveness_route_state(route) \
					== NetwMultiplayer.ENTITY_STATE_LIVE
	)
	assert_bool(c1_live).override_failure_message(
		"[%s] host spawn never reached client 1" % world.provider(),
	).is_true()

	var c2_live := await world.pump_until(
		func() -> bool:
			return c2.liveness_route_state(route) \
					== NetwMultiplayer.ENTITY_STATE_LIVE
	)
	assert_bool(c2_live).override_failure_message(
		"[%s] host spawn never reached client 2" % world.provider(),
	).is_true()


func _scenario_declared_scene_comes_online(world: NetwEmbeddingWorld) -> void:
	world.declare_initial_scene(_LEVEL)
	var host := await world.host()

	var online := await world.pump_until(
		func() -> bool: return not host.scene_list().is_empty()
	)
	assert_bool(online).override_failure_message(
		"[%s] declared initial scene never came online on the host"
		% world.provider(),
	).is_true()


func _scenario_service_configures_on_mount(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	world.mount_clock()

	var configured := await world.pump_until(
		func() -> bool: return host.clock_is_configured()
	)
	assert_bool(configured).override_failure_message(
		"[%s] a clock service mounted under the host never configured the session"
		% world.provider(),
	).is_true()

#endregion

#region Conformance cases (each scenario × each provider)

func test_host_comes_online_scoped() -> void:
	await _scenario_host_comes_online(_scoped())


func test_host_comes_online_root() -> void:
	await _scenario_host_comes_online(_root())


func test_client_is_admitted_scoped() -> void:
	await _scenario_client_is_admitted(_scoped())


func test_client_is_admitted_root() -> void:
	await _scenario_client_is_admitted(_root())


func test_roster_crosses_both_ways_scoped() -> void:
	await _scenario_roster_crosses_both_ways(_scoped())


func test_roster_crosses_both_ways_root() -> void:
	await _scenario_roster_crosses_both_ways(_root())


func test_spawn_reaches_every_peer_scoped() -> void:
	await _scenario_spawn_reaches_every_peer(_scoped())


func test_spawn_reaches_every_peer_root() -> void:
	await _scenario_spawn_reaches_every_peer(_root())


func test_declared_scene_comes_online_scoped() -> void:
	await _scenario_declared_scene_comes_online(_scoped())


func test_declared_scene_comes_online_root() -> void:
	await _scenario_declared_scene_comes_online(_root())


func test_service_configures_on_mount_scoped() -> void:
	await _scenario_service_configures_on_mount(_scoped())


func test_service_configures_on_mount_root() -> void:
	await _scenario_service_configures_on_mount(_root())

#endregion
