extends NetwTestSuite

class ActionBody extends LagCompSimBody:
	var action: NetwAction
	var ghost_count := 0
	var confirmed_count := 0
	var denied_count := 0
	var server_requests := 0
	var last_view_tick := -1
	var last_requested_tick := -1
	var last_execution_tick := -1

	@onready var _ctx := Netw.of(self)


	func _ready() -> void:
		action = _ctx.lag_compensation.action(_server_action)
		action.predict = _predict
		action.confirmed.connect(func() -> void: confirmed_count += 1)
		action.denied.connect(func() -> void: denied_count += 1)


	func fire(tick: int) -> void:
		action.request(tick)


	func fire_tick_aligned(tick: int) -> void:
		action.timing_mode = NetwAction.TimingMode.TICK_ALIGNED
		action.request(tick)


	func fire_immediate(tick: int) -> void:
		action.timing_mode = NetwAction.TimingMode.IMMEDIATE
		action.request(tick)


	func fire_state_ready(tick: int) -> void:
		action.timing_mode = NetwAction.TimingMode.TICK_ALIGNED_STATE_READY
		action.request(tick)


	func _predict() -> Node:
		var ghost := Node.new()
		ghost.name = &"Ghost"
		add_child(ghost)
		ghost_count += 1
		return ghost


	func _server_action(ctx: NetwAction.Context) -> void:
		server_requests += 1
		last_view_tick = ctx.view_tick
		last_requested_tick = ctx.requested_tick
		last_execution_tick = ctx.execution_tick
		ctx.deny()


func test_action_prediction_denial_and_revert_flow() -> void:
	var s := await _new_scenario()
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody

	client_body.fire(s.client_clock.tick)
	assert_int(client_body.ghost_count).is_equal(1)
	assert_that(client_body.get_node_or_null("Ghost")).is_not_null()

	s.run_until(
		func() -> bool:
			return client_body.denied_count == 1,
		30,
	)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(client_body.denied_count).is_equal(1)
	await get_tree().process_frame
	assert_that(client_body.get_node_or_null("Ghost")).is_null()
	await s.teardown()


func test_action_timing_modes_flow() -> void:
	var s := await _new_scenario()
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
	var fire_tick := s.server_clock.tick + 3

	client_body.fire_tick_aligned(fire_tick)
	assert_int(client_body.ghost_count).is_equal(1)
	s.run(3)

	assert_int(server_body.server_requests).is_equal(0)
	s.run(1)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_view_tick).is_equal(fire_tick)
	assert_int(server_body.last_requested_tick).is_equal(fire_tick)
	assert_int(server_body.last_execution_tick).is_equal(fire_tick)
	await s.teardown()

	s = await _new_scenario()
	p = await s.add_predicted_entity()
	client_body = p.client_root as ActionBody
	server_body = p.server_root as ActionBody
	var future_limit := s.server_sim.max_future_action_ticks

	client_body.fire_tick_aligned(s.server_clock.tick + future_limit + 4)
	s.run_until(
		func() -> bool:
			return client_body.denied_count == 1,
		30,
	)

	assert_int(server_body.server_requests).is_equal(0)
	assert_int(client_body.denied_count).is_equal(1)
	await s.teardown()

	s = await _new_scenario()
	p = await s.add_predicted_entity()
	client_body = p.client_root as ActionBody
	server_body = p.server_root as ActionBody
	var requested_tick := s.server_clock.tick + 3

	client_body.fire_immediate(requested_tick)
	s.run_until(
		func() -> bool:
			return server_body.server_requests == 1,
		30,
	)

	assert_int(server_body.last_requested_tick).is_equal(requested_tick)
	assert_int(server_body.last_view_tick).is_less_equal(
		server_body.last_execution_tick,
	)
	await s.teardown()

	s = await _new_scenario()
	p = await s.add_predicted_entity()
	client_body = p.client_root as ActionBody
	server_body = p.server_root as ActionBody
	requested_tick = s.server_clock.tick + 6

	client_body.fire(requested_tick)
	s.run_until(
		func() -> bool:
			return server_body.server_requests == 1,
		30,
	)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_requested_tick).is_equal(requested_tick)
	assert_int(server_body.last_execution_tick).is_less(requested_tick)
	await s.teardown()


func test_state_ready_gate_flow() -> void:
	var s := await _new_scenario()
	s.server_sim.input_gate_deadline_ticks = 24
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
	# Real starvation: the input stream rides a slow uplink, so the server's
	# consumed state trails the view tick by the link latency and the gate holds
	# until the delayed inputs arrive and consume through it. 8 polls at 60 Hz
	# physics is 4 ticks at the 30 Hz tickrate, so the gate cannot open before
	# the fire tick plus the latency.
	s.latency_up(8)
	var fire_tick := s.server_clock.tick + 3
	var timeline := s.server_sim.timeline_of(p.server_entity)

	client_body.fire_state_ready(fire_tick)
	s.run(5)

	assert_int(server_body.server_requests).is_equal(0)
	s.run_until(
		func() -> bool:
			return server_body.server_requests == 1,
		20,
	)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_view_tick).is_equal(fire_tick)
	assert_bool(timeline.state_at(fire_tick).is_empty()).is_false()
	assert_int(s.server_sim.metrics()[&"gate_fallbacks"]).is_equal(0)
	await s.teardown()

	s = await _new_scenario()
	s.server_sim.input_gate_deadline_ticks = 2
	p = await s.add_predicted_entity()
	client_body = p.client_root as ActionBody
	server_body = p.server_root as ActionBody
	fire_tick = s.server_clock.tick + 3
	var fallback_ticks: Array[int] = []
	s.server_sim.action_gate_fallback.connect(
		func(_key: StringName, _view_tick: int) -> void:
			fallback_ticks.append(_view_tick),
	)

	client_body.fire_state_ready(fire_tick)
	s.run(6)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_view_tick).is_equal(fire_tick)
	assert_int(fallback_ticks.size()).is_equal(1)
	assert_int(fallback_ticks[0]).is_equal(fire_tick)
	assert_int(s.server_sim.metrics()[&"gate_fallbacks"]).is_equal(1)
	await s.teardown()

	s = await _new_scenario()
	s.server_sim.input_gate_deadline_ticks = 12
	p = await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
	)
	# Starve the uplink outright (60 polls is 30 ticks, longer than the case) so
	# only the hand-fed rows below drive readiness and the gap tick stays lost,
	# exercising the STALL step-over instead of a live stream healing it.
	s.latency_up(60)
	server_body = p.server_root as ActionBody
	var target := s.server.get_path_to(server_body)
	timeline = s.server_sim.timeline_of(p.server_entity)
	s.run(2)
	var first_input_tick := s.server_clock.tick
	var view_tick := first_input_tick + 2
	var key := s.server.api.lag_compensation.effects.key_for(
		p.server_entity,
		view_tick,
		0,
	)

	# Feed rows tick-ascending: newest_input_tick() reads insertion order, so the
	# view-tick row must land last for the consume step to see it as the later
	# arrival that steps over the lost gap tick.
	p.server_prediction.record_server_input(
		first_input_tick,
		{ &"motion": Vector2.RIGHT },
	)
	timeline.record_input(view_tick, { &"motion": Vector2.RIGHT })
	s.server_sim._send_action_request(
		target,
		&"_server_action",
		view_tick,
		null,
		key,
		NetwAction.TimingMode.TICK_ALIGNED_STATE_READY,
	)
	s.run_until(
		func() -> bool:
			return server_body.server_requests == 1,
		12,
	)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_view_tick).is_equal(view_tick)
	assert_bool(timeline.state_at(view_tick).is_empty()).is_false()
	assert_int(p.server_prediction.missing_count).is_greater(0)
	assert_int(s.server_sim.metrics()[&"gate_fallbacks"]).is_equal(0)
	await s.teardown()


func test_action_key_and_adoption_flow() -> void:
	var s := await _new_scenario()
	var p := await s.add_predicted_entity()
	var client_key := s.client.api.lag_compensation.effects.key_for(
		p.client_entity,
		44,
		0,
	)
	var server_key := s.server.api.lag_compensation.effects.key_for(
		p.server_entity,
		44,
		0,
	)

	assert_that(client_key).is_equal(server_key)

	var client_body := p.client_root as ActionBody
	var key := s.client.api.lag_compensation.effects.key_for(
		p.client_entity,
		s.client_clock.tick,
		0,
	)

	client_body.fire(s.client_clock.tick)
	assert_that(client_body.get_node_or_null("Ghost")).is_not_null()
	s.client.api.lag_compensation.effects.adopt(key)
	await get_tree().process_frame

	assert_int(client_body.confirmed_count).is_equal(1)
	assert_that(client_body.get_node_or_null("Ghost")).is_null()
	await s.teardown()

	s = await _new_scenario()
	p = await s.add_predicted_entity()
	client_body = p.client_root as ActionBody
	var custom_confirm_called := [0]

	client_body.action.confirm = func(_ghost: Node) -> void:
		custom_confirm_called[0] += 1

	key = s.client.api.lag_compensation.effects.key_for(
		p.client_entity,
		s.client_clock.tick,
		0,
	)

	client_body.fire(s.client_clock.tick)
	assert_that(client_body.get_node_or_null("Ghost")).is_not_null()
	s.client.api.lag_compensation.effects.adopt(key)
	await get_tree().process_frame

	assert_int(client_body.confirmed_count).is_equal(1)
	assert_int(custom_confirm_called[0]).is_equal(1)
	assert_that(client_body.get_node_or_null("Ghost")).is_not_null()

	client_body.get_node("Ghost").queue_free()
	await get_tree().process_frame
	await s.teardown()


func _new_scenario() -> PredictionScenario:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self, PredictionScenario.TICKRATE, PredictionScenario.DISPLAY_OFFSET, false)
	return s
