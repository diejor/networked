## Real ENet gate for the complete session lifecycle.
class_name TestEnetSessionLifecycle
extends NetwTestSuite

func test_host_join_kick_rejoin_and_leave() -> void:
	var host := await EnetTestSupport.start_host(self)
	assert_that(host).is_not_empty()
	var host_tree: MultiplayerTree = host.tree

	var kicked_client := EnetTestSupport.make_client_tree(
		self,
		host.port,
		"Kicked",
	)
	assert_int(await _join(kicked_client, host.port, &"kicked")).is_equal(OK)
	var kicked_id := kicked_client.api.get_unique_id()

	host_tree.api.session.kick(kicked_id, "gate")

	assert_bool(await _wait_peer_absent(host_tree.api, kicked_id)).is_true()
	await EnetTestSupport.stop_tree(kicked_client)

	var leaving_client := EnetTestSupport.make_client_tree(
		self,
		host.port,
		"Leaving",
	)
	assert_int(await _join(leaving_client, host.port, &"leaving")) \
			.is_equal(OK)
	var leaving_id := leaving_client.api.get_unique_id()

	await leaving_client.api.session.leave()

	assert_int(leaving_client.api.session.state) \
			.is_equal(NetwSessionInterface.State.OFFLINE)
	assert_bool(await _wait_peer_absent(host_tree.api, leaving_id)).is_true()

	await EnetTestSupport.stop_tree(leaving_client)
	await EnetTestSupport.stop_tree(host_tree)


func _join(
		tree: MultiplayerTree,
		port: int,
		username: StringName,
) -> Error:
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": port }
	var payload := JoinPayload.new()
	payload.username = username
	return await tree.join(target, payload, 2.0, true)


func _wait_peer_absent(api: NetwMultiplayer, peer_id: int) -> bool:
	var deadline := Time.get_ticks_msec() + 2000
	while peer_id in api.get_peers() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	return peer_id not in api.get_peers()
