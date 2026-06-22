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

	@onready var _ctx := Netw.ctx(self)


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


func test_request_denial_reverts_predicted_ghost() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
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


func test_future_tick_action_waits_for_server_tick() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
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


func test_input_gated_action_waits_for_consumed_state() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	s.server_sim.input_gate_deadline_ticks = 12
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
	var fire_tick := s.server_clock.tick + 3
	var timeline := s.server_sim.timeline_of(p.server_entity)
	p.server_input.timeline = NetwTimeline.new()

	client_body.fire_state_ready(fire_tick)
	s.run(5)

	assert_int(server_body.server_requests).is_equal(0)
	timeline.record_input(fire_tick, { &"motion": Vector2.ZERO })
	s.run(1)

	assert_int(server_body.server_requests).is_equal(0)
	s.feed_server_input(p, fire_tick - 1, { &"motion": Vector2.ZERO })
	s.run(2)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_view_tick).is_equal(fire_tick)
	assert_bool(timeline.state_at(fire_tick).is_empty()).is_false()


func test_input_gated_late_action_waits_for_consumed_state() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	s.server_sim.input_gate_deadline_ticks = 12
	var p := await s.add_predicted_entity()
	var server_body := p.server_root as ActionBody
	var target := s.server.get_path_to(server_body)
	p.server_input.timeline = NetwTimeline.new()
	s.run(2)
	var fire_tick := s.server_clock.tick
	var key := s.server.lag_compensation.effects.key_for(
		p.server_entity,
		fire_tick,
		0,
	)
	var timeline := s.server_sim.timeline_of(p.server_entity)

	s.server_sim._request_action(
		target,
		&"_server_action",
		fire_tick,
		null,
		key,
		NetwAction.TimingMode.TICK_ALIGNED_STATE_READY,
	)
	s.run(1)

	assert_int(server_body.server_requests).is_equal(0)
	timeline.record_input(fire_tick, { &"motion": Vector2.ZERO })
	s.run(1)

	assert_int(server_body.server_requests).is_equal(0)
	s.feed_server_input(p, fire_tick - 1, { &"motion": Vector2.ZERO })
	s.run(2)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_view_tick).is_equal(fire_tick)


func test_input_gated_action_executes_after_deadline() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	s.server_sim.input_gate_deadline_ticks = 2
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
	var fire_tick := s.server_clock.tick + 3
	var fallback_ticks: Array[int] = []
	p.server_input.timeline = NetwTimeline.new()
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


func test_state_ready_action_releases_on_missing_policy_slot() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	s.server_sim.input_gate_deadline_ticks = 12
	var p := await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
	)
	var server_body := p.server_root as ActionBody
	var target := s.server.get_path_to(server_body)
	p.server_input.timeline = NetwTimeline.new()
	var timeline := s.server_sim.timeline_of(p.server_entity)
	s.run(2)
	var first_input_tick := s.server_clock.tick
	var view_tick := first_input_tick + 2
	var key := s.server.lag_compensation.effects.key_for(
		p.server_entity,
		view_tick,
		0,
	)

	timeline.record_input(first_input_tick, { &"motion": Vector2.RIGHT })
	timeline.record_input(view_tick, { &"motion": Vector2.RIGHT })
	p.server_prediction._on_server_input(
		first_input_tick,
		{ &"motion": Vector2.RIGHT },
	)
	s.server_sim._request_action(
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


func test_far_future_action_is_denied() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
	var future_limit := s.server_sim.max_future_action_ticks

	client_body.fire_tick_aligned(s.server_clock.tick + future_limit + 4)
	s.run_until(
		func() -> bool:
			return client_body.denied_count == 1,
		30,
	)

	assert_int(server_body.server_requests).is_equal(0)
	assert_int(client_body.denied_count).is_equal(1)


func test_immediate_action_clamps_future_tick() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
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


func test_default_action_mode_is_immediate() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var server_body := p.server_root as ActionBody
	var requested_tick := s.server_clock.tick + 6

	client_body.fire(requested_tick)
	s.run_until(
		func() -> bool:
			return server_body.server_requests == 1,
		30,
	)

	assert_int(server_body.server_requests).is_equal(1)
	assert_int(server_body.last_requested_tick).is_equal(requested_tick)
	assert_int(server_body.last_execution_tick).is_less(requested_tick)


func test_action_key_matches_across_peer_entities() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var client_key := s.client.lag_compensation.effects.key_for(
		p.client_entity,
		44,
		0,
	)
	var server_key := s.server.lag_compensation.effects.key_for(
		p.server_entity,
		44,
		0,
	)

	assert_that(client_key).is_equal(server_key)


func test_adopted_action_frees_predicted_ghost() -> void:
	var s := PredictionScenario.new()
	s.body_type = ActionBody
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var client_body := p.client_root as ActionBody
	var key := s.client.lag_compensation.effects.key_for(
		p.client_entity,
		s.client_clock.tick,
		0,
	)

	client_body.fire(s.client_clock.tick)
	assert_that(client_body.get_node_or_null("Ghost")).is_not_null()
	s.client.lag_compensation.effects.adopt(key)
	await get_tree().process_frame

	assert_int(client_body.confirmed_count).is_equal(1)
	assert_that(client_body.get_node_or_null("Ghost")).is_null()
