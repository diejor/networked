extends NetwTestSuite

func test_key_for_is_deterministic_and_namespaced() -> void:
	var root := Node.new()
	auto_free(root)
	NetwEntity.bind(root, &"player", 7)
	var entity := NetwEntity.of(root)
	var effects := NetwEffects.new()

	assert_that(effects.key_for(entity, 12, 3)).is_equal(&"act__player__12__3")


func test_key_survives_entity_name_transport() -> void:
	var root := Node.new()
	auto_free(root)
	NetwEntity.bind(root, &"player", 7)
	var effects := NetwEffects.new()
	var key := effects.key_for(NetwEntity.of(root), 12, 3)
	var spawned := Node.new()
	auto_free(spawned)

	NetwEntity.bind(spawned, key, 0)

	assert_that(NetwEntity.parse_entity(spawned.name)).is_equal(key)
	assert_that(NetwEntity.of(spawned).entity_id).is_equal(key)


func test_adopt_drops_pending_without_revert() -> void:
	var service := LagCompensationService.new()
	auto_free(service)
	var effects := service.effects
	var reverted := { &"value": false }

	effects.arm(
		&"act__test__1__0",
		func() -> void:
			reverted[&"value"] = true,
		10,
	)
	effects.adopt(&"act__test__1__0")
	effects.discard(&"act__test__1__0")

	assert_bool(reverted[&"value"]).is_false()


func test_discard_runs_revert_once() -> void:
	var service := LagCompensationService.new()
	auto_free(service)
	var effects := service.effects
	var reverted := { &"count": 0 }

	effects.arm(
		&"act__test__1__0",
		func() -> void:
			reverted[&"count"] += 1,
		10,
	)
	effects.discard(&"act__test__1__0")
	effects.discard(&"act__test__1__0")

	assert_int(reverted[&"count"]).is_equal(1)


func test_timeout_discards_pending_effect() -> void:
	var service := LagCompensationService.new()
	auto_free(service)
	var effects := service.effects
	var reverted := { &"value": false }

	effects.arm(
		&"act__test__1__0",
		func() -> void:
			reverted[&"value"] = true,
		2,
	)
	service._on_tick(0.0, 1)
	assert_bool(reverted[&"value"]).is_false()
	service._on_tick(0.0, 2)
	assert_bool(reverted[&"value"]).is_true()


func test_observer_adopts_already_bound_entity() -> void:
	var service := LagCompensationService.new()
	auto_free(service)
	var effects := service.effects
	var confirmed := { &"value": false }
	var node := Node.new()
	auto_free(node)
	NetwEntity.bind(node, &"act__test__4__0", 0)

	effects.arm(&"act__test__4__0", func() -> void: pass, 10)
	service._watch_action(
		&"act__test__4__0",
		func() -> void:
			confirmed[&"value"] = true,
		Callable(),
	)
	service._observe_node_entity_ref(weakref(node))

	assert_bool(confirmed[&"value"]).is_true()


func test_observer_adopts_from_entity_child() -> void:
	var service := LagCompensationService.new()
	auto_free(service)
	var effects := service.effects
	var confirmed := { &"value": false }
	var node := Node.new()
	auto_free(node)
	var child := Node.new()
	auto_free(child)
	NetwEntity.bind(node, &"act__test__5__0", 0)
	node.add_child(child)

	effects.arm(&"act__test__5__0", func() -> void: pass, 10)
	service._watch_action(
		&"act__test__5__0",
		func() -> void:
			confirmed[&"value"] = true,
		Callable(),
	)
	service._observe_node_entity_ref(weakref(child))

	assert_bool(confirmed[&"value"]).is_true()
