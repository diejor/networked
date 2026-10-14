class_name TestNakamaServerTransfer
extends NetwTestSuite

const TIMEOUT := 15.0
const DEADLINE_TICKS := 30
const DEADLINE_WATCH_SECONDS := 3.0
const CRATE_STRIDE := 20.0
const MOVED_BEFORE := Vector2(200, 100)
const MOVED_AFTER := Vector2(400, 300)
const DEADLINE_WARNING := "did not finish its world report"

var trees: Array[MultiplayerViewport] = []
var driver: RelayDriver
var warnings: WarningLog


class RelayEndpoint:
	extends MultiplayerPeerExtension

	const PUMP_MS := 8

	signal server_relabelled(successor_was: int, former_now: int)

	var wrapper: NakamaWrapper
	var relay: NakamaRelayPeer
	var target := 0


	func _init(p_wrapper: NakamaWrapper) -> void:
		wrapper = p_wrapper
		relay = wrapper.peer()
		relay.peer_connected.connect(peer_connected.emit)
		relay.peer_disconnected.connect(peer_disconnected.emit)
		relay.server_relabelled.connect(server_relabelled.emit)


	func pump() -> void:
		OS.delay_msec(PUMP_MS)
		wrapper._socket._adapter._process(0.0)


	func transfer_server(peer_id: int) -> Error:
		var answer: Error = relay.transfer_server(peer_id)
		pump()
		return answer


	func _poll() -> void:
		pump()
		relay.poll()


	func _get_packet_script() -> PackedByteArray:
		return relay.get_packet()


	func _get_packet_peer() -> int:
		return relay.get_packet_peer()


	func _get_available_packet_count() -> int:
		return relay.get_available_packet_count()


	func _get_packet_mode() -> TransferMode:
		return TRANSFER_MODE_RELIABLE


	func _get_packet_channel() -> int:
		return 0


	func _put_packet_script(buffer: PackedByteArray) -> Error:
		relay.set_target_peer(target)
		return relay.put_packet(buffer)


	func _get_max_packet_size() -> int:
		return NakamaRelayPeer.MAX_PACKET_SIZE


	func _set_transfer_channel(_channel: int) -> void:
		pass


	func _get_transfer_channel() -> int:
		return 0


	func _set_transfer_mode(_mode: TransferMode) -> void:
		pass


	func _get_transfer_mode() -> TransferMode:
		return TRANSFER_MODE_RELIABLE


	func _set_target_peer(peer_id: int) -> void:
		target = peer_id


	func _is_server() -> bool:
		return relay.get_unique_id() == 1


	func _is_server_relay_supported() -> bool:
		return relay.is_server_relay_supported()


	func _get_unique_id() -> int:
		return relay.get_unique_id()


	func _get_connection_status() -> ConnectionStatus:
		return relay.get_connection_status()


	func _set_refuse_new_connections(_enable: bool) -> void:
		pass


	func _is_refusing_new_connections() -> bool:
		return false


	func _disconnect_peer(peer_id: int, force: bool) -> void:
		relay.disconnect_peer(peer_id, force)


	func _close() -> void:
		pass


class RelayDriver:
	extends NetwTransferDriver

	const SEATS := 4
	const CLOSE_PUMPS := 100

	var prepared: Array = []
	var seated: Array[NakamaWrapper] = []
	var made: Array[NakamaWrapper] = []


	func prepare(host: Node, label: String) -> bool:
		var wrappers: Array[NakamaWrapper] = []
		for seat in SEATS:
			var wrapper := NakamaWrapper.new()
			made.append(wrapper)
			var config := {
				"host": NakamaTestServer.host(),
				"device_id": "%s-%d" % [label, seat],
				"username": "%s-%d" % [label, seat],
			}
			var result: Dictionary = await wrapper.connect_async(host, config)
			if not result.ok:
				return false
			wrapper.migrate_on_host_loss = true
			wrappers.append(wrapper)
		wrappers[0].create_match()
		await wrappers[0].match_joined
		for seat in range(1, SEATS):
			wrappers[seat].join_match(wrappers[0].match_id())
			await wrappers[seat].match_joined
		var deadline := Time.get_ticks_msec() + 5000
		while not wrappers.all(meshed):
			if Time.get_ticks_msec() > deadline:
				return false
			await host.get_tree().process_frame
		prepared.append(wrappers)
		return true


	func meshed(wrapper: NakamaWrapper) -> bool:
		return wrapper.peer()._get_peers().size() == SEATS - 1


	func make_endpoints(count: int) -> Array[MultiplayerPeer]:
		var endpoints: Array[MultiplayerPeer] = []
		if prepared.is_empty():
			return endpoints
		seated.assign(prepared.pop_front())
		for seat in mini(count, seated.size()):
			endpoints.append(RelayEndpoint.new(seated[seat]))
		return endpoints


	func lose_server() -> void:
		var socket = seated[0]._socket
		socket.close()
		for pump in CLOSE_PUMPS:
			OS.delay_msec(RelayEndpoint.PUMP_MS)
			socket._adapter._process(0.0)
			if not socket.is_connected_to_host():
				return


	func close() -> void:
		for wrapper in seated:
			wrapper.leave()
		seated.clear()


	func leave_all() -> void:
		for wrapper in made:
			wrapper.leave()
		made.clear()
		prepared.clear()


class WarningLog:
	extends Logger

	var lines: Array[String] = []
	var mutex := Mutex.new()


	func _log_error(
			_function: String,
			_file: String,
			_line: int,
			code: String,
			rationale: String,
			_editor_notify: bool,
			_error_type: int,
			_script_backtraces: Array[ScriptBacktrace],
	) -> void:
		mutex.lock()
		lines.append(code if rationale.is_empty() else rationale)
		mutex.unlock()


	func count(fragment: String) -> int:
		mutex.lock()
		var found := lines.filter(
			func(line: String) -> bool: return line.contains(fragment)
		).size()
		mutex.unlock()
		return found


class TransferWorld:
	extends Node

	func _init(mode: int) -> void:
		name = "World"
		Netw.configure_session(self) \
				.transfer(mode) \
				.transfer_deadline(DEADLINE_TICKS)
		Netw.configure_clock(self)
		Netw.configure_spawn(spawn_arena)
		Netw.configure_join(enter)


	func spawn_arena() -> Node:
		return Arena.new()


	func enter(player: NetwPlayer) -> void:
		var arena := get_node_or_null(^"Arena") as Arena
		if arena == null:
			arena = Netw.spawn(spawn_arena)
			add_child(arena)
			arena.add_child(Netw.spawn(arena.spawn_crate))
		arena.add_child(Netw.spawn_player(player, arena.spawn_avatar))


class Arena:
	extends Node2D

	func _init() -> void:
		name = "Arena"
		Netw.configure_scene(self).labeled(&"Arena")
		Netw.configure_spawn(spawn_avatar)
		Netw.configure_spawn(spawn_crate)


	func spawn_avatar() -> Node:
		return Avatar.new()


	func spawn_crate() -> Node:
		return Crate.new()


class Avatar:
	extends Node2D

	func _init() -> void:
		var declared := Netw.configure_entity(self)
		declared.initial_controller = NetwEntity.INITIAL_PLAYER
		Netw.configure_property(self, &"position").broadcast()


class Crate:
	extends Node2D

	func _init() -> void:
		Netw.configure_entity(self)
		Netw.configure_property(self, &"position").state()


	func _physics_process(_delta: float) -> void:
		if is_multiplayer_authority():
			position.x += 1.0


class Watch:
	extends RefCounted

	var transfers: Array = []
	var server_lost := false


	func _init(tree: MultiplayerViewport) -> void:
		Netw.session(tree).server_transferred.connect(on_server_transferred)
		tree.api.server_disconnected.connect(on_server_disconnected)


	func on_server_transferred(successor_was: int, former_now: int) -> void:
		transfers.append([successor_was, former_now])


	func on_server_disconnected() -> void:
		server_lost = true


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	warnings = WarningLog.new()
	OS.add_logger(warnings)


func after_test() -> void:
	OS.remove_logger(warnings)
	if driver != null:
		driver.leave_all()
		driver = null
	for tree in trees:
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	trees.clear()
	await super.after_test()


func test_the_relay_bridge_meets_the_transfer_contract() -> void:
	driver = RelayDriver.new()
	var label := "netw-kit-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	for scenario in 2:
		var ready := await driver.prepare(self, "%s-%d" % [label, scenario])
		assert_bool(ready).override_failure_message("a match never meshed").is_true()
		if not ready:
			return
	var parent := Node.new()
	add_child(parent)
	var contract := NetwTransferContract.new(driver, parent)
	var failures := contract.run()
	parent.queue_free()
	assert_array(failures) \
			.override_failure_message("\n".join(failures)) \
			.is_empty()
	for scenario in [&"lost", &"handed_on"]:
		for law in ["C1", "C2", "C3", "C4", "C5", "C6", "C7", "inner"]:
			assert_array(contract.judged).contains(["%s x %s" % [law, scenario]])


func test_a_lost_host_continues_in_rejoin_mode() -> void:
	await judge_loss(NetwSession.TRANSFER_REJOIN)


func test_a_lost_host_continues_in_continue_mode() -> void:
	await judge_loss(NetwSession.TRANSFER_CONTINUE)


func test_a_handed_off_server_continues_in_rejoin_mode() -> void:
	await judge_hand_off(NetwSession.TRANSFER_REJOIN)


func test_a_handed_off_server_continues_in_continue_mode() -> void:
	await judge_hand_off(NetwSession.TRANSFER_CONTINUE)


func judge_loss(mode: int) -> void:
	var host := await open_world("host", mode)
	var room := Netw.session(host).connection.join_address
	var ana := await join_world(room, "ana", mode)
	var ben := await join_world(room, "ben", mode)
	var survivors: Array[MultiplayerViewport] = [ana, ben]
	if not await wait_until(world_whole.bind([host, ana, ben]), "the world"):
		return
	await judge_state_flows(host, ana, MOVED_BEFORE)
	var ana_id := ana.multiplayer.get_unique_id()
	var ben_id := ben.multiplayer.get_unique_id()
	var successor_was := mini(ana_id, ben_id)
	var watches: Array[Watch] = [Watch.new(ana), Watch.new(ben)]

	trees.erase(host)
	await NakamaTestSupport.stop_tree(host)

	if not await wait_until(world_whole.bind(survivors), "the survivors"):
		return
	for watch in watches:
		assert_array(watch.transfers).is_equal([[successor_was, 0]])
		assert_bool(watch.server_lost).is_false()
	var successor := ana if ana_id == successor_was else ben
	var other := ben if successor == ana else ana
	assert_int(successor.multiplayer.get_unique_id()).is_equal(1)
	assert_bool(successor.multiplayer.is_server()).is_true()
	assert_int(other.multiplayer.get_unique_id()).is_equal(maxi(ana_id, ben_id))
	for tree in survivors:
		assert_object(body_of(tree, "host")).is_null()
	await judge_state_flows(successor, other, MOVED_AFTER)
	await judge_deadline_holds(survivors)

	var dana := await join_world(room, "dana", mode)
	await wait_until(world_whole.bind([successor, other, dana]), "a late joiner")


func judge_hand_off(mode: int) -> void:
	var host := await open_world("host", mode)
	var room := Netw.session(host).connection.join_address
	var ana := await join_world(room, "ana", mode)
	var ben := await join_world(room, "ben", mode)
	var everyone: Array[MultiplayerViewport] = [host, ana, ben]
	if not await wait_until(world_whole.bind(everyone), "the world"):
		return
	await judge_state_flows(host, ana, MOVED_BEFORE)
	var ana_id := ana.multiplayer.get_unique_id()
	var watches: Array[Watch] = [Watch.new(host), Watch.new(ana), Watch.new(ben)]

	assert_int(Netw.session(host).transfer_server(ana_id)).is_equal(OK)

	var handed := func() -> bool:
		return watches.all(func(watch: Watch) -> bool: return not watch.transfers.is_empty())
	if not await wait_until(handed, "every peer to install the hand off"):
		return
	if not await wait_until(world_whole.bind(everyone), "the handed off world"):
		return
	var former_now: int = watches[0].transfers[0][1]
	assert_int(former_now).is_not_equal(0)
	for watch in watches:
		assert_array(watch.transfers).is_equal([[ana_id, former_now]])
		assert_bool(watch.server_lost).is_false()
	assert_int(ana.multiplayer.get_unique_id()).is_equal(1)
	assert_int(host.multiplayer.get_unique_id()).is_equal(former_now)
	assert_bool(host.multiplayer.is_server()).is_false()
	await judge_state_flows(ana, host, MOVED_AFTER)
	await judge_deadline_holds(everyone)


func judge_state_flows(
		server: MultiplayerViewport,
		client: MultiplayerViewport,
		to: Vector2,
) -> void:
	var start := crate_of(client).position.x
	await wait_until(
		func() -> bool: return crate_of(client).position.x > start + CRATE_STRIDE,
		"the crate to move under the server",
	)
	var client_name := String(Netw.session(client).local_player.username)
	(body_of(client, client_name) as Node2D).position = to
	await wait_until(
		func() -> bool: return body_of(server, client_name).position == to,
		"the client's avatar to reach the server",
	)
	var server_name := String(Netw.session(server).local_player.username)
	(body_of(server, server_name) as Node2D).position = to
	await wait_until(
		func() -> bool: return body_of(client, server_name).position == to,
		"the server's avatar to reach the client",
	)


func judge_deadline_holds(survivors: Array[MultiplayerViewport]) -> void:
	await get_tree().create_timer(DEADLINE_WATCH_SECONDS).timeout
	assert_int(warnings.count(DEADLINE_WARNING)).is_equal(0)
	for tree in survivors:
		assert_bool(tree.api.is_online).is_true()
		assert_int(tree.multiplayer.get_peers().size()).is_equal(survivors.size() - 1)


func open_world(username: String, mode: int) -> MultiplayerViewport:
	var tree := NakamaTestSupport.make_client_tree(self, username)
	tree.add_child(TransferWorld.new(mode))
	trees.append(tree)
	await get_tree().process_frame
	var settled := await NetwOrdinarySetup.connect_through(
		tree,
		NakamaTestSupport.PEER_CLASS,
		NetwConnection.TRANSPORT_MODE_HOST,
		"",
		{ },
		StringName(username),
	)
	assert_int(settled.error).is_equal(OK)
	migrate(tree)
	return tree


func join_world(room: String, username: String, mode: int) -> MultiplayerViewport:
	var tree := NakamaTestSupport.make_client_tree(self, username)
	tree.add_child(TransferWorld.new(mode))
	trees.append(tree)
	await get_tree().process_frame
	var error := await NakamaTestSupport.join_client(tree, room, username)
	assert_int(error).is_equal(OK)
	migrate(tree)
	return tree


func migrate(tree: MultiplayerViewport) -> void:
	NakamaTestSupport.directory(tree).wrapper().migrate_on_host_loss = true


func world_whole(group: Array) -> bool:
	for tree: MultiplayerViewport in group:
		if not tree.api.is_online or crate_of(tree) == null:
			return false
		if tree.multiplayer.get_peers().size() != group.size() - 1:
			return false
		for other: MultiplayerViewport in group:
			var player := Netw.session(other).local_player
			if player == null or body_of(tree, player.username) == null:
				return false
		if tree.api.bodies.size() != group.size():
			return false
	return true


func body_of(tree: MultiplayerViewport, username: String) -> Node2D:
	var wanted := StringName(username)
	for body: NetwEntity in tree.api.bodies:
		if body.player != null and body.player.username == wanted:
			return body.owner as Node2D
	return null


func crate_of(tree: MultiplayerViewport) -> Crate:
	var arena := tree.get_node_or_null(^"World/Arena")
	if arena == null:
		return null
	for child in arena.get_children():
		if child is Crate:
			return child
	return null


func wait_until(condition: Callable, label: String, timeout: float = TIMEOUT) -> bool:
	var deadline := Time.get_ticks_msec() + int(timeout * 1000.0)
	while not condition.call():
		if Time.get_ticks_msec() > deadline:
			assert_bool(false) \
					.override_failure_message("Timed out waiting for %s." % label) \
					.is_true()
			return false
		await get_tree().process_frame
	return true
