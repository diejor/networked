## Integration test for the host fallback: two peers press Host on one machine
## and land in one session rather than two, and the loser of the port reports
## success because its verb succeeded.
class_name TestHostFallbackLandsInOneSession
extends NetwTestSuite

const _PORT := 29411


func after_test() -> void:
	super.after_test()


func _tree(parent: Node, name: String) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = name
	tree.auto_host_headless = false
	var params := NetwENetParams.new()
	params.port = _PORT
	tree.transport = params
	parent.add_child(tree)
	return tree


func _payload(username: StringName) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	return payload


func test_the_second_host_joins_the_first_instead_of_failing() -> void:
	var root: Node = auto_free(Node.new())
	add_child(root)
	var first := _tree(root, "First")
	var second := _tree(root, "Second")

	var first_result := await NetwConnector.of(first.api).host(_payload(&"valeria"))
	assert_bool(first_result.is_ok()).is_true()
	assert_bool(first.api.is_host).is_true()

	# The port is taken, so the verb falls back to joining whoever took it. The
	# losing attempt is not the outcome: the verb call is.
	var outcomes: Array[NetwConnectResult] = []
	NetwConnector.of(second.api).finished.connect(func(r): outcomes.append(r))
	var attempts: Array[NetwConnectAttempt] = []
	NetwConnector.of(second.api).attempt_started.connect(func(a): attempts.append(a))

	# ENet reports the taken port as a runtime error on its way to the fallback,
	# which is the engine saying exactly what this test is about.
	var landed: Array[NetwConnectResult] = []
	await assert_error(
		func() -> void:
			landed.append(await NetwConnector.of(second.api).host(_payload(&"jose")))
	).is_push_error("Couldn't create an ENet host.")
	var second_result := landed[0]

	assert_bool(second_result.is_ok()).is_true()
	assert_bool(second.api.is_local_client).is_true()
	assert_bool(second.api.is_host).is_false()
	# More than one attempt ran, and exactly one outcome was reported, which is
	# what keeps a fallback from raising a failure banner on its way to success.
	assert_int(attempts.size()).is_greater(1)
	assert_int(outcomes.size()).is_equal(1)
	assert_bool(outcomes[0].is_ok()).is_true()

	# One session: the host sees the second peer as a participant.
	var joined: bool = await _await_until(
		func() -> bool:
			return first.api.get_peers().size() == 1
	)
	assert_bool(joined).is_true()

	await EnetTestSupport.stop_tree(second)
	await EnetTestSupport.stop_tree(first)


func _await_until(predicate: Callable, seconds: float = 4.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < deadline:
		if predicate.call():
			return true
		await get_tree().process_frame
	return predicate.call()
