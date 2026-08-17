## Flat optimistic-effect ledger laws.
extends NetwTestSuite

const _DEFAULT_TIMEOUT_TICKS := 120

var mt: MultiplayerTree
var api: NetwMultiplayer
var clock_node: MultiplayerClock


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "EffectsTree"
	clock_node = MultiplayerClock.new()
	clock_node.name = "MultiplayerClock"
	clock_node.tickrate = 30
	clock_node.set_physics_process(false)
	mt.add_child(clock_node)
	var service := LagCompensation.new()
	service.name = "LagCompensation"
	mt.add_child(service)
	add_child(mt)
	auto_free(mt)
	api = mt.api


# Steps until the sweep has been handed [param tick], which is the tick a
# deadline is measured against.
func _sweep_through(tick: int) -> void:
	while api._clock.tick <= tick:
		api._clock.force_step(1)


func test_key_is_deterministic_and_namespaced() -> void:
	var root := make_test_entity(mt, "PlayerBody", 0, false)
	NetwEntity.bind(root, &"player", 7)

	assert_that(api.effect_key(api.entity_of(root), 12, 3)) \
			.is_equal(&"act__player__12__3")


func test_key_without_an_entity_is_session_scoped() -> void:
	assert_that(api.effect_key(RID(), 12, 3)).is_equal(&"act__12__3")


func test_key_survives_entity_name_transport() -> void:
	var root := make_test_entity(mt, "PlayerBody", 0, false)
	NetwEntity.bind(root, &"player", 7)
	var key := api.effect_key(api.entity_of(root), 12, 3)
	var spawned := Node.new()
	auto_free(spawned)

	NetwEntity.bind(spawned, key, 0)

	assert_that(NetwEntity.parse_entity(spawned.name)).is_equal(key)
	assert_that(NetwEntity.of(spawned).entity_id).is_equal(key)


func test_adopt_drops_pending_without_revert() -> void:
	var seen := { &"reverted": false, &"confirmed": false, &"denied": false }

	api.effect_arm(
		&"act__test__1__0",
		func() -> void:
			seen[&"reverted"] = true,
		10,
	)
	api.effect_watch(
		&"act__test__1__0",
		func() -> void:
			seen[&"confirmed"] = true,
		func() -> void:
			seen[&"denied"] = true,
	)
	api.effect_adopt(&"act__test__1__0")
	api.effect_discard(&"act__test__1__0")

	assert_bool(seen[&"reverted"]).is_false()
	assert_bool(seen[&"confirmed"]).is_true()
	assert_bool(seen[&"denied"]).is_false()
	assert_bool(api.effect_pending(&"act__test__1__0")).is_false()


func test_discard_runs_revert_once_and_denies_the_watcher() -> void:
	var seen := { &"reverts": 0, &"denials": 0 }

	api.effect_arm(
		&"act__test__1__0",
		func() -> void:
			seen[&"reverts"] += 1,
		10,
	)
	api.effect_watch(
		&"act__test__1__0",
		Callable(),
		func() -> void:
			seen[&"denials"] += 1,
	)
	api.effect_discard(&"act__test__1__0")
	api.effect_discard(&"act__test__1__0")

	assert_int(seen[&"reverts"]).is_equal(1)
	assert_int(seen[&"denials"]).is_equal(1)


func test_a_watcher_on_an_unarmed_key_is_refused() -> void:
	var seen := { &"confirmed": false }

	api.effect_watch(
		&"act__test__1__0",
		func() -> void:
			seen[&"confirmed"] = true,
		Callable(),
	)
	api.effect_arm(&"act__test__1__0", Callable(), 10)
	api.effect_adopt(&"act__test__1__0")

	assert_bool(seen[&"confirmed"]).is_false()


func test_timeout_discards_pending_effect() -> void:
	var seen := { &"reverted": false, &"denied": false }

	api.effect_arm(
		&"act__test__1__0",
		func() -> void:
			seen[&"reverted"] = true,
		2,
	)
	api.effect_watch(
		&"act__test__1__0",
		Callable(),
		func() -> void:
			seen[&"denied"] = true,
	)

	_sweep_through(1)
	assert_bool(seen[&"reverted"]).is_false()

	_sweep_through(2)
	assert_bool(seen[&"reverted"]).is_true()
	assert_bool(seen[&"denied"]).is_true()


func test_an_unstated_timeout_takes_the_session_default() -> void:
	var seen := { &"reverted": false }

	api.effect_arm(
		&"act__test__1__0",
		func() -> void:
			seen[&"reverted"] = true,
	)

	_sweep_through(_DEFAULT_TIMEOUT_TICKS - 1)
	assert_bool(seen[&"reverted"]).is_false()

	_sweep_through(_DEFAULT_TIMEOUT_TICKS)
	assert_bool(seen[&"reverted"]).is_true()


func test_observer_adopts_already_bound_entity() -> void:
	var seen := { &"confirmed": false }
	var node := Node.new()
	auto_free(node)
	NetwEntity.bind(node, &"act__test__4__0", 0)

	api.effect_arm(&"act__test__4__0", Callable(), 10)
	api.effect_watch(
		&"act__test__4__0",
		func() -> void:
			seen[&"confirmed"] = true,
		Callable(),
	)
	api._lagcomp._observe_node_entity_ref(weakref(node))

	assert_bool(seen[&"confirmed"]).is_true()


func test_observer_adopts_from_entity_child() -> void:
	var seen := { &"confirmed": false }
	var node := Node.new()
	auto_free(node)
	var child := Node.new()
	auto_free(child)
	NetwEntity.bind(node, &"act__test__5__0", 0)
	node.add_child(child)

	api.effect_arm(&"act__test__5__0", Callable(), 10)
	api.effect_watch(
		&"act__test__5__0",
		func() -> void:
			seen[&"confirmed"] = true,
		Callable(),
	)
	api._lagcomp._observe_node_entity_ref(weakref(child))

	assert_bool(seen[&"confirmed"]).is_true()


func test_the_census_counts_what_is_armed() -> void:
	api.effect_arm(&"act__test__1__0", Callable(), 10)
	api.effect_arm(&"act__test__2__0", Callable(), 10)

	assert_int(api.lagcomp_metrics()[&"effects_armed"]).is_equal(2)

	api.effect_adopt(&"act__test__1__0")

	assert_int(api.lagcomp_metrics()[&"effects_armed"]).is_equal(1)
