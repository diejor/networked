class_name TestNakamaPeerAwareness
extends NetwTestSuite

const _TIMEOUT := 10.0

var _trees: Array = []

@warning_ignore("unused_parameter")
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


func test_peers_and_rosters_see_each_other() -> void:
	var host := await NakamaTestSupport.start_host(self, "host")
	var host_tree := host.tree as MultiplayerTree
	_track(host_tree)

	var client_a := NakamaTestSupport.make_client_tree(self, "alice")
	var client_b := NakamaTestSupport.make_client_tree(self, "bruno")
	_track(client_a)
	_track(client_b)

	var client_a_err: Error = await client_a.join(
		NakamaTestSupport.make_join_target(client_a, host.room),
		NakamaTestSupport.payload("alice"),
		_TIMEOUT,
	)
	var client_b_err: Error = await client_b.join(
		NakamaTestSupport.make_join_target(client_b, host.room),
		NakamaTestSupport.payload("bruno"),
		_TIMEOUT,
	)
	assert_int(client_a_err).is_equal(OK)
	assert_int(client_b_err).is_equal(OK)

	await _await(
		func() -> bool:
			return _all_trees_see_all(host_tree, client_a, client_b),
		"all Nakama peers to see each other",
	)

	for tree in [host_tree, client_a, client_b]:
		assert_int(tree.multiplayer.get_peers().size()).is_equal(2)
		assert_int(tree.get_joined_players().size()).is_equal(3)


func test_disconnect_propagates_to_remaining_peers() -> void:
	var host := await NakamaTestSupport.start_host(self, "host")
	var host_tree := host.tree as MultiplayerTree
	_track(host_tree)

	var client_a := NakamaTestSupport.make_client_tree(self, "alice")
	var client_b := NakamaTestSupport.make_client_tree(self, "bruno")
	_track(client_a)
	_track(client_b)

	var client_a_err: Error = await client_a.join(
		NakamaTestSupport.make_join_target(client_a, host.room),
		NakamaTestSupport.payload("alice"),
		_TIMEOUT,
	)
	var client_b_err: Error = await client_b.join(
		NakamaTestSupport.make_join_target(client_b, host.room),
		NakamaTestSupport.payload("bruno"),
		_TIMEOUT,
	)
	assert_int(client_a_err).is_equal(OK)
	assert_int(client_b_err).is_equal(OK)

	await _await(
		func() -> bool:
			return _all_trees_see_all(host_tree, client_a, client_b),
		"all Nakama peers to connect before disconnect",
	)

	var client_a_id := client_a.multiplayer.get_unique_id()
	_trees.erase(client_a)
	await NakamaTestSupport.stop_tree(client_a)

	await _await(
		func() -> bool:
			return _client_was_dropped(host_tree, client_b, client_a_id),
		"remaining peers to drop disconnected client",
	)

	var client_b_saw_server_disconnect := [false]
	client_b.server_disconnected.connect(
		func() -> void:
			client_b_saw_server_disconnect[0] = true,
	)

	_trees.erase(host_tree)
	await NakamaTestSupport.stop_tree(host_tree)

	await _await(
		func() -> bool:
			return _server_disconnect_seen(client_b, client_b_saw_server_disconnect),
		"client to observe host disconnect",
	)


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


func _sees_all(tree: MultiplayerTree) -> bool:
	return tree.is_online() \
			and tree.multiplayer.get_peers().size() == 2 \
			and _joined_count(tree) == 3


func _all_trees_see_all(
		host_tree: MultiplayerTree,
		client_a: MultiplayerTree,
		client_b: MultiplayerTree,
) -> bool:
	return _sees_all(host_tree) \
			and _sees_all(client_a) \
			and _sees_all(client_b)


func _client_was_dropped(
		host_tree: MultiplayerTree,
		client_b: MultiplayerTree,
		client_a_id: int,
) -> bool:
	return not _has_peer(host_tree, client_a_id) \
			and not _has_peer(client_b, client_a_id) \
			and _joined_count(host_tree) == 2


func _server_disconnect_seen(
		client_b: MultiplayerTree,
		client_b_saw_server_disconnect: Array,
) -> bool:
	return client_b_saw_server_disconnect[0] or not client_b.is_online()


func _has_peer(tree: MultiplayerTree, peer_id: int) -> bool:
	if not is_instance_valid(tree) or tree.multiplayer == null:
		return false
	return tree.multiplayer.get_peers().has(peer_id)


func _joined_count(tree: MultiplayerTree) -> int:
	if not is_instance_valid(tree):
		return 0
	return tree.get_joined_players().size()
