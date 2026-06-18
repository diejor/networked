extends NetwTestSuite

class ActionBody extends LagCompSimBody:
	var action: NetwAction
	var ghost_count := 0
	var confirmed_count := 0
	var denied_count := 0
	var server_requests := 0

	@onready var _ctx := Netw.ctx(self)


	func _ready() -> void:
		action = _ctx.lag_compensation.action(_server_action)
		action.predict = _predict
		action.confirmed.connect(func() -> void: confirmed_count += 1)
		action.denied.connect(func() -> void: denied_count += 1)


	func fire(tick: int) -> void:
		action.request(tick)


	func _predict() -> Node:
		var ghost := Node.new()
		ghost.name = &"Ghost"
		add_child(ghost)
		ghost_count += 1
		return ghost


	func _server_action(ctx: NetwAction.Context) -> void:
		server_requests += 1
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
