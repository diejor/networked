class_name TestNakamaReplication
extends NetwTestSuite

const MAIN := preload("res://examples/quick_start/Main.tscn")
const _TIMEOUT := 10.0

var _trees: Array = []


func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func after_test() -> void:
	for tree in _trees.duplicate():
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	_trees.clear()
	await super.after_test()


func test_quick_start_players_spawn_and_replicate() -> void:
	var host := await NakamaTestSupport.host_scene(self, MAIN, "valeria")
	var host_tree := host.tree as MultiplayerTree
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
		"all quick start players to spawn on all peers",
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
	var input := _input_for(_find_player(jose_tree, "jose"))
	var input_start := jose_on_host.position.x
	input.state[&"move_right"] = true
	await _await(
		func() -> bool:
			return _player_x_greater(host_tree, "jose", input_start + 8.0),
		"client input to move server player",
	)
	input.state[&"move_right"] = false


func _track(tree: MultiplayerTree) -> void:
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


func _has_players(tree: MultiplayerTree, usernames: Array[String]) -> bool:
	if not is_instance_valid(tree):
		return false
	for username in usernames:
		if _find_player(tree, username) == null:
			return false
	return true


func _all_players_spawned(
		host_tree: MultiplayerTree,
		jose_tree: MultiplayerTree,
		mia_tree: MultiplayerTree,
) -> bool:
	var expected: Array[String] = ["valeria", "jose", "mia"]
	return _has_players(host_tree, expected) \
			and _has_players(jose_tree, expected) \
			and _has_players(mia_tree, expected)


func _player_x_greater(
		tree: MultiplayerTree,
		username: String,
		threshold: float,
) -> bool:
	var player := _find_player(tree, username) as Node2D
	return player != null and player.position.x > threshold


func _find_player(tree: MultiplayerTree, username: String) -> Node:
	for player in tree.get_all_players():
		var entity := NetwEntity.of(player)
		if entity and entity.entity_id == StringName(username):
			return player
		if StringName(NetwEntity.parse_entity(player.name)) == StringName(username):
			return player
	return null


func _input_for(player: Node) -> MoveInputComponent:
	return player.get_node("%InputComponent") as MoveInputComponent
