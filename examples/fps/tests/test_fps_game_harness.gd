extends NetwTestSuite

const MAIN := preload("res://examples/fps/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func after_test() -> void:
	Input.action_release("move_forward")
	Input.action_release("shoot")


func test_the_host_sees_a_client_player_move() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Main", 2.0)
	await client.await_scene(&"Main", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var host_view := await host.await_player(&"luigi", 2.0)
	assert_that(client.local_player).is_equal(own)
	assert_bool(host_view.entity.is_controlled_locally).is_false()

	await game.sync_ticks(30)
	var start: Vector3 = own.position
	var host_start: Vector3 = host_view.position
	Input.action_press("move_forward")
	await game.sync_ticks(60)
	Input.action_release("move_forward")
	await game.sync_ticks(10)

	var travelled: float = own.position.distance_to(start)
	assert_float(travelled).is_greater(1.0)
	assert_float(host_view.position.distance_to(host_start)) \
			.override_failure_message(
				"the host view must follow the move its controller relayed",
			).is_greater(travelled * 0.5)


func test_a_client_shot_lands_on_an_enemy_every_peer_sees() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	var host_level := (await host.await_scene(&"Main", 2.0)).root
	var client_level := (await client.await_scene(&"Main", 2.0)).root
	var own := await client.await_player(&"luigi", 2.0)
	var host_view := await host.await_player(&"luigi", 2.0)
	var mario := await host.await_player(&"mario", 2.0)
	var host_enemy: Node3D = host_level.get_node(^"Enemies/enemy-flying")
	var client_enemy: Node3D = client_level.get_node(^"Enemies/enemy-flying")
	var confirmed := [0]
	own.shoot.confirmed.connect(func() -> void: confirmed[0] += 1)
	await game.sync_ticks(30)

	mario.camera.rotation.x = deg_to_rad(90)
	var aim: Vector3 = client_enemy.global_position + Vector3(0, 0.25, 0) \
			- own.camera.global_position
	own.rotation.y = atan2(-aim.x, -aim.z)
	own.camera.rotation.x = atan2(aim.y, Vector2(aim.x, aim.z).length())
	Input.action_press("shoot")
	await game.sync_ticks(2)
	Input.action_release("shoot")
	await game.sync_ticks(30)

	assert_int(confirmed[0]).override_failure_message(
		"the server must run the shot action the client requested",
	).is_greater(0)
	assert_int(host_enemy.health).override_failure_message(
		"the server must take the hit the client reported",
	).is_less(100)
	assert_int(client_enemy.health).override_failure_message(
		"the client must see the hit the server took",
	).is_equal(host_enemy.health)
	assert_int(own.shots).is_greater(0)
	assert_int(host_view.shots).override_failure_message(
		"the host must see the shot counter the client raised",
	).is_equal(own.shots)
