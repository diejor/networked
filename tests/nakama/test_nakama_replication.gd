class_name TestNakamaReplication
extends NetwTestSuite

const MAIN := preload("res://examples/multiple_levels/main.tscn")
const _TIMEOUT := 10.0

var _trees: Array = []


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestSupport.skip_reason(MAIN) != "",
		skip_reason = NakamaTestSupport.skip_reason(MAIN),
) -> void:
	pass


func after_test() -> void:
	for tree in _trees.duplicate():
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	_trees.clear()
	await super.after_test()


func test_multiple_levels_players_spawn_and_replicate() -> void:
	var host := await NakamaTestSupport.host_scene(self, MAIN, "valeria")
	var host_tree := host.tree as MultiplayerViewport
	_track(host_tree)

	var jose_tree := await NakamaTestSupport.join_scene(
		self,
		MAIN,
		host.room,
		"jose",
	)
	var mia_tree := await NakamaTestSupport.join_scene(
		self,
		MAIN,
		host.room,
		"mia",
	)
	_track(jose_tree)
	_track(mia_tree)

	await _await(
		func() -> bool:
			return _all_players_spawned(host_tree, jose_tree, mia_tree),
		"all multiple levels players to spawn on all peers",
	)

	var host_valeria := _find_player(host_tree, "valeria") as Node2D
	var jose_valeria := _find_player(jose_tree, "valeria") as Node2D
	var start := jose_valeria.position.x

	host_valeria.position.x += 96.0
	await _await(
		func() -> bool:
			return _player_x_greater(jose_tree, "valeria", start + 32.0),
		"host position to replicate to client",
	)

	var jose_on_host := _find_player(host_tree, "jose") as Node2D
	var player := _find_player(jose_tree, "jose")
	var input_start := jose_on_host.position.x
	player.pressed[&"move_right"] = true
	await _await(
		func() -> bool:
			return _player_x_greater(host_tree, "jose", input_start + 8.0),
		"client input to move server player",
	)
	player.pressed[&"move_right"] = false


func _track(tree: MultiplayerViewport) -> void:
	_trees.append(tree)


func _await(
		cond: Callable,
		label: String,
		timeout: float = _TIMEOUT,
) -> void:
	var deadline := get_tree().create_timer(timeout)
	while deadline.time_left > 0.0:
		if cond.call():
			return
		await get_tree().process_frame
	assert_bool(cond.call()) \
			.override_failure_message("Timed out waiting for %s." % label) \
			.is_true()


func _has_players(tree: MultiplayerViewport, usernames: Array[String]) -> bool:
	if not is_instance_valid(tree):
		return false
	for username in usernames:
		if _find_player(tree, username) == null:
			return false
	return true


func _all_players_spawned(
		host_tree: MultiplayerViewport,
		jose_tree: MultiplayerViewport,
		mia_tree: MultiplayerViewport,
) -> bool:
	var expected: Array[String] = ["valeria", "jose", "mia"]
	return _has_players(host_tree, expected) \
			and _has_players(jose_tree, expected) \
			and _has_players(mia_tree, expected)


func _player_x_greater(
		tree: MultiplayerViewport,
		username: String,
		threshold: float,
) -> bool:
	var player := _find_player(tree, username) as Node2D
	return player != null and player.position.x > threshold


func _find_player(tree: MultiplayerViewport, username: String) -> Node:
	var username_id := StringName(username)
	for player: NetwEntity in tree.api.player_entities:
		if player.name == username_id:
			return player.owner
	return null
