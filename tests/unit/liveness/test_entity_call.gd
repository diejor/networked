## Unit tests for the replication core carrier, Netw facade, transactions, var sync, signals, and component tables.
class_name TestEntityCall
extends NetwTestSuite

# Dynamic test node helper
class TestTargetNode:
	extends Node

	signal greeted(msg: String)

	var health := 100


	@rpc("any_peer", "call_remote", "reliable") func greet(msg: String, value: int) -> void:
		greeted.emit(msg + str(value))


	@rpc("any_peer", "call_remote", "reliable") func greet_node(target_node: Node, value: int) -> void:
		pass


	var last_node: Node = null
	var last_node_value := -1


	@rpc("any_peer", "call_remote", "reliable") func receive_node(n: Variant, value: int) -> void:
		last_node = n
		last_node_value = value


	var admin_action_called := false


	@rpc("authority", "call_remote", "reliable") func admin_action() -> void:
		admin_action_called = true


	var last_yaw := 0.0
	var last_dir := Vector2.ZERO


	@rpc("any_peer", "call_remote", "unreliable") func aim(yaw: float, dir: Vector2) -> void:
		last_yaw = yaw
		last_dir = dir


	var last_sender := -1


	@rpc("any_peer", "call_remote", "reliable") func record_sender() -> void:
		last_sender = multiplayer.get_remote_sender_id()


	@rpc("any_peer", "call_remote", "reliable") func request_val(x: int) -> int:
		return x * 2


	@rpc("any_peer", "call_remote", "reliable") func request_async(x: int) -> NetwPromise:
		var p := NetwPromise.new()
		call_deferred("_resolve_promise", p, x * 3)
		return p


	func _resolve_promise(p: NetwPromise, val: int) -> void:
		p.resolve(val)


# Records what the carrier hands the transport. The suite's subject is the
# datagram a send produces, and the carrier reaches the transport rather than
# any script surface, so the recording peer is where the bytes are still
# observable. It reports every id the suite addresses as connected, because the
# carrier refuses a peer the transport does not have.
class RecordingPeer:
	extends MultiplayerPeerExtension

	const REACHABLE: Array[int] = [1, 2, 3, 5]

	var sent: Array[Dictionary] = []

	var _target := 0
	var _reliable := true


	func announce_peers() -> void:
		for id in REACHABLE:
			peer_connected.emit(id)


	func _get_connection_status() -> ConnectionStatus:
		return CONNECTION_CONNECTED


	func _get_unique_id() -> int:
		return 1


	func _is_server() -> bool:
		return true


	# A relay would wrap the datagram in a SYS envelope addressed to the
	# server, and the bytes a send produced would no longer be the bytes here.
	func _is_server_relay_supported() -> bool:
		return false


	func _get_available_packet_count() -> int:
		return 0


	func _set_target_peer(id: int) -> void:
		_target = id


	func _set_transfer_channel(_channel: int) -> void:
		pass


	func _get_transfer_channel() -> int:
		return 0


	func _set_transfer_mode(mode: TransferMode) -> void:
		_reliable = mode == TRANSFER_MODE_RELIABLE


	func _get_transfer_mode() -> TransferMode:
		return TRANSFER_MODE_RELIABLE if _reliable else TRANSFER_MODE_UNRELIABLE


	func _put_packet_script(buffer: PackedByteArray) -> Error:
		sent.append(
			{
				"peer_id": _target,
				"bytes": buffer,
				"reliable": _reliable,
			},
		)
		return OK


	func _poll() -> void:
		pass


	func _close() -> void:
		pass


	func _disconnect_peer(_id: int, _force: bool) -> void:
		pass


# Subclass NetwMultiplayer to fake the local id the send branches read.
class TestNetwMultiplayer:
	extends NetwMultiplayer

	var recorder: RecordingPeer

	# When non-negative, this peer pretends to be that unique id, so a test can
	# exercise the client-side send branches without a second machine.
	var fake_unique_id := -1

	# Every datagram the carrier produced, stripped back to the frames it
	# carries. The transport's own raw-command byte and the carrier header ride
	# in front of them, and neither is what a case here is about.
	var sent_packets: Array[Dictionary]:
		get:
			var out: Array[Dictionary] = []
			for row in recorder.sent:
				var datagram: PackedByteArray = row["bytes"].slice(1)
				var header := NetwCarrierFrame.read(datagram)
				if header.kind == NetwCarrierFrame.Kind.FOREIGN:
					continue
				if header.kind == NetwCarrierFrame.Kind.MALFORMED:
					continue
				out.append(
					{
						"peer_id": row["peer_id"],
						"bytes": datagram.slice(header.payload_offset),
						"reliable": row["reliable"],
					},
				)
			return out


	func clear_sent() -> void:
		recorder.sent.clear()


	func _get_unique_id() -> int:
		if fake_unique_id >= 0:
			return fake_unique_id
		return super._get_unique_id()


# Installs the capturing API through the tree's one construction point.
class CaptureTree:
	extends MultiplayerTree

	func _make_api() -> NetwMultiplayer:
		return TestNetwMultiplayer.new(SceneMultiplayer.new())


var mt: MultiplayerTree
var api: TestNetwMultiplayer


func before_test() -> void:
	mt = CaptureTree.new()
	mt.name = "TestTree"

	add_child(mt)
	auto_free(mt)
	api = mt.api as TestNetwMultiplayer

	var peer := RecordingPeer.new()
	api.recorder = peer
	# The transport learns the peers so a send is not refused for addressing one
	# it does not have. Nothing joined a session here, so the arrivals must not
	# reach the session: the session's handshake would hold each one pending
	# forever and the blocked signals keep the admissions inside the transport.
	# The handshake goes back afterwards, because a case that installs its own
	# peer expects to find the session as the session left it.
	var handshake := api.inner.auth_callback
	api.inner.auth_callback = Callable()
	api.inner.multiplayer_peer = peer
	api.inner.set_block_signals(true)
	peer.announce_peers()
	api.inner.set_block_signals(false)
	api.inner.auth_callback = handshake


func _bound_entity(route: int, node: Node = null) -> NetwEntity:
	var owner_node = node
	if not owner_node:
		owner_node = Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	mt.add_child(owner_node)
	auto_free(owner_node)

	# Mock multiplayer API to make is_server() and authority check work
	var mock_api := SceneMultiplayer.new()
	owner_node.set_meta(&"_multiplayer_api", mock_api)

	var rid := api.entity_of(entity.owner)
	api.entity_bind_route(rid, route)
	return entity


func test_local_rpc_call_dispatches() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	var received: Array[String] = []
	node.greeted.connect(
		func(msg: String) -> void:
			received.append(msg)
	)

	Netw.rpc(node.greet, ["test", 42])

	assert_that(received.size()).is_equal(1)
	assert_that(received[0]).is_equal("test42")


func test_node_argument_crosses_as_route() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	var passenger = _bound_entity(2)

	# Call to peer 2 to force it to go to the wire instead of local dispatch
	Netw.rpc_id(2, node.greet_node, [passenger.owner, 0])

	assert_that(api.sent_packets.size()).is_equal(1)
	var packet = api.sent_packets[0]
	assert_that(packet["peer_id"]).is_equal(2)

	var frames = NetwFrameEnvelope.unpack_all(packet["bytes"])
	assert_that(frames.size()).is_equal(1)

	var env = frames[0]
	assert_that(env["channel"]).is_equal(NetwFrameEnvelope.Channel.CALL)

	# Decode CALL payload: [ flag u8 | target_peer u32 | method token | per-arg stream ]
	var r = NetwBitBufferReader.create(env["payload"])
	var flag = r.get_aligned_u8()
	var target_type = flag & 3
	assert_that(target_type).is_equal(2)
	var target_peer = r.get_aligned_u32()
	assert_that(target_peer).is_equal(2)
	var _method_raw = NetwScriptModel.read_method_token(r)
	var args: Array = NetwScriptModel.read_call_args(r, [], [])

	# The node argument crosses as a NetwNodeRef, not a value.
	var ref = args[0]
	assert_that(ref is NetwNodeRef).is_true()
	assert_that(ref.route).is_equal(2)


func test_component_argument_crosses_as_route_comp() -> void:
	# A child component passed as an argument crosses as its entity route plus
	# its component id, not as a NodePath.
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	var child = Node.new()
	child.name = "Child"
	node.add_child(child)
	entity.register_component(child)
	entity.components.hydrate()
	var comp_id = entity.components.id_for_path(NodePath("Child"))

	Netw.rpc_id(2, node.receive_node, [child, 0])

	assert_that(api.sent_packets.size()).is_equal(1)
	var frames = NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	var r = NetwBitBufferReader.create(frames[0]["payload"])
	var _flag = r.get_aligned_u8()
	var _target_peer = r.get_aligned_u32()
	var _method = NetwScriptModel.read_method_token(r)
	var args: Array = NetwScriptModel.read_call_args(r, [], [])

	var ref = args[0]
	assert_that(ref is NetwNodeRef).is_true()
	assert_that(ref.route).is_equal(1)
	assert_that(ref.comp).is_equal(comp_id)


func test_component_argument_resolves_on_receive() -> void:
	# The receiver rebinds the (route, comp) reference to its own instance of the
	# child, so the handler is called with the resolved component node.
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	var child = Node.new()
	child.name = "Child"
	node.add_child(child)
	entity.register_component(child)
	entity.components.hydrate()
	Netw.configure_rpc(node.receive_node)
	var comp_id = entity.components.id_for_path(NodePath("Child"))

	var ref := NetwNodeRef.create(1, comp_id, "")
	var call_payload := _pack_call(0, &"receive_node", [ref, 7])
	api._drive_carrier(
		1,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.CALL,
			call_payload,
		),
		true,
	)

	assert_that(node.last_node).is_equal(child)


func test_node_argument_outside_sender_interest_is_not_disclosed() -> void:
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	var hidden = _bound_entity(2)
	Netw.configure_rpc(node.receive_node)
	api._interest.layer(&"hidden").add_entity(hidden)

	var ref := NetwNodeRef.create(2, 0, "")
	var call_payload := _pack_call(0, &"receive_node", [ref, 7])
	api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.CALL,
			call_payload,
		),
		true,
	)

	assert_object(node.last_node).is_null()
	assert_int(node.last_node_value).is_equal(-1)


func test_entity_root_argument_crosses_as_root_ref() -> void:
	# An entity root crosses as a NetwNodeRef with component 0, the root address.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	var passenger = _bound_entity(2)

	Netw.rpc_id(3, node.receive_node, [passenger.owner, 0])

	var frames = NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	var r = NetwBitBufferReader.create(frames[0]["payload"])
	var _flag = r.get_aligned_u8()
	var _target_peer = r.get_aligned_u32()
	var _method = NetwScriptModel.read_method_token(r)
	var args: Array = NetwScriptModel.read_call_args(r, [], [])

	var ref = args[0]
	assert_that(ref is NetwNodeRef).is_true()
	assert_that(ref.route).is_equal(2)
	assert_that(ref.comp).is_equal(0)


func test_call_on_addresses_an_entity_component() -> void:
	var node = TestTargetNode.new()
	var entity := _bound_entity(1, node)
	Netw.configure_rpc(node.greet)

	var verdict := api.entity_call(
		entity.rid,
		0,
		&"greet",
		["hello", 42],
		2,
	)

	assert_int(verdict).is_equal(OK)
	assert_int(api.sent_packets.size()).is_equal(1)
	var frames := NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	assert_int(frames[0]["route"]).is_equal(1)
	assert_int(frames[0]["channel"]).is_equal(
		NetwFrameEnvelope.Channel.CALL,
	)


func test_request_resolves_synchronously() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	var p = Netw.request(node.request_val, [21])
	assert_that(p).is_not_null()
	assert_that(p.is_completed).is_true()
	assert_that(p.result).is_equal(42)


func test_async_request_resolves_deferred() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	var p = Netw.request(node.request_async, [10])
	assert_that(p).is_not_null()
	assert_that(p.is_completed).is_false()

	# Drain deferred calls
	await NetwTestSuite.drain_frames(get_tree(), 2)

	assert_that(p.is_completed).is_true()
	assert_that(p.result).is_equal(30)


func test_reply_value_roundtrips_through_codec() -> void:
	# A reply value crosses through the value codec and resolves its promise.
	var node = TestTargetNode.new()
	_bound_entity(1, node)
	var promise: NetwPromise = api._rpc_core.request_call(
		2,
		node.request_val,
		[1],
		99999,
	)
	api.clear_sent()

	# Reply to peer 2 so it rides the wire instead of loopback-dispatching.
	api._rpc_core.send_reply(2, 5, 1, "hello")

	var frames = NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	var env = frames[0]
	assert_that(env["channel"]).is_equal(NetwFrameEnvelope.Channel.REPLY)
	api._drive_carrier(2, api.sent_packets[0]["bytes"], true)

	assert_that(promise.is_completed).is_true()
	assert_that(promise.result).is_equal("hello")


func test_request_timeout() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	# Request to peer 2 so it is tracked under txn book instead of executing locally
	var p = Netw.request_id(2, node.request_val, [1])
	assert_that(p.is_completed).is_false()
	assert_that(p.is_failed).is_false()

	# Advance clock/frame count to trigger sweep timeout (default timeout is 150 frames/ticks)
	for i in 151:
		api.poll()

	assert_that(p.is_failed).is_true()
	assert_that(p.code).is_equal(ERR_TIMEOUT)


func test_component_table_indexing() -> void:
	var owner_node = Node2D.new()
	var entity = _bound_entity(1, owner_node)

	var comp_a = Node.new()
	comp_a.name = "CompA"
	owner_node.add_child(comp_a)

	var comp_b = Node.new()
	comp_b.name = "CompB"
	owner_node.add_child(comp_b)

	entity.register_component(comp_a)
	entity.register_component(comp_b)

	entity.components.hydrate()

	# Lexicographical sort check: CompA should be 1, CompB should be 2
	assert_that(entity.components.path_for_id(1)).is_equal(NodePath("CompA"))
	assert_that(entity.components.path_for_id(2)).is_equal(NodePath("CompB"))
	assert_that(entity.components.id_for_path(NodePath("CompA"))).is_equal(1)
	assert_that(entity.components.id_for_path(NodePath("CompB"))).is_equal(2)
	assert_that(entity.components.poisoned).is_false()
	assert_that(entity.components.table_hash).is_not_equal(0)


func test_component_table_divergence_guard() -> void:
	var owner_node = Node2D.new()
	var entity = _bound_entity(1, owner_node)

	var comp = Node.new()
	comp.name = "CompA"
	owner_node.add_child(comp)
	entity.register_component(comp)

	# Hydrate components to assign table hash
	entity.components.hydrate()

	# Simulate a mismatched table hash delivered by the server's spawn packet.
	entity.components.wire_hash = 9999

	# Fake client-ness: _on_identity_hydrated's server check reads the owning
	# tree's role, not any per-node authority stamp. A peer must also be
	# present, or "no peer connected" wins as the offline-counts-as-server
	# default (matching NetwEntity._is_route_authority's convention).
	var fake_peer := LocalMultiplayerPeer.new()
	fake_peer.create_client(2)
	mt.api._set_multiplayer_peer(fake_peer)
	mt.role = NetwMultiplayer.Role.CLIENT
	entity._stamp_multiplayer(api)
	assert_bool(entity.is_authority).is_false()

	# Trigger client-side hydrated check
	await assert_error(
		func() -> void:
			entity._on_identity_hydrated()
	).is_push_error(GdUnitArgumentMatchers.any())

	assert_that(entity.components.poisoned).is_true()


# A PROPERTY_SYNC payload is the property token then the value, whatever the
# configured transfer mode. Freshness for an unreliable send rides the carrier
# datagram's sequence stamp, never the payload.
func _serialize_prop(property: StringName, value: Variant) -> PackedByteArray:
	var w := NetwBitBufferWriter.new()
	NetwScriptModel.write_token(w, property)
	NetwCodec.write_values(w, [value], [], [typeof(value)])
	return w.to_bytes()


func test_var_synchronization() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	# 1. Authority write (always allowed)
	var payload := _serialize_prop(&"health", 80)
	api._drive_carrier(
		1,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.PROPERTY_SYNC,
			payload,
		),
		true,
	)
	assert_that(node.health).is_equal(80)

	# 2. Non-authority write (default write policy is authority only)
	node.health = 80
	var payload2 := _serialize_prop(&"health", 50)
	api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.PROPERTY_SYNC,
			payload2,
		),
		true,
	)
	assert_that(node.health).is_equal(80)

	# 3. Non-authority write with any_peer policy configured
	Netw.configure_property(node, "health").any_peer().call_local()
	api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.PROPERTY_SYNC,
			payload2,
		),
		true,
	)
	assert_that(node.health).is_equal(50)


func test_property_sync_unreliable_out_of_order() -> void:
	var node = TestTargetNode.new()
	_bound_entity(1, node)
	Netw.configure_property(node, "health").unreliable()

	# Staleness for unreliable property sync is judged per carrier datagram,
	# so out-of-order protection only exists on the wire path. Deliver whole
	# datagrams the way the raw byte channel would.
	api._on_inner_peer_packet(1, _prop_datagram(5, 1, &"health", 70))
	assert_that(node.health).is_equal(70)

	api._on_inner_peer_packet(1, _prop_datagram(3, 1, &"health", 60))
	assert_that(node.health).is_equal(70)

	api._on_inner_peer_packet(1, _prop_datagram(10, 1, &"health", 50))
	assert_that(node.health).is_equal(50)


func test_property_sync_payload_is_token_then_value_only() -> void:
	var node = TestTargetNode.new()
	_bound_entity(1, node)
	Netw.configure_property(node, "health").unreliable()
	node.health = 70

	# Send as a client so the frame reaches the wire instead of loopback.
	api.fake_unique_id = 2
	api._replication.send_property(node, &"health")
	api.fake_unique_id = -1

	assert_that(api.sent_packets.size()).is_equal(1)
	var sent: Dictionary = api.sent_packets[0]
	assert_that(sent["peer_id"]).is_equal(1)
	assert_that(sent["reliable"]).is_equal(false)

	# The payload is exactly the property token and the value. An unreliable
	# send carries no freshness bytes of its own, because staleness is judged
	# by the carrier datagram's sequence stamp.
	var frame: Dictionary = NetwFrameEnvelope.unpack_all(sent["bytes"])[0]
	var r := NetwBitBufferReader.create(frame["payload"])
	NetwScriptModel.read_token(r)
	var values := NetwCodec.read_values(r, [null], [TYPE_INT])
	assert_that(values[0]).is_equal(70)
	assert_that(r.remaining_bytes()).is_equal(0)

	# The token above is a 1-byte id into a per-script name table, and the two
	# peers never exchange that table. It agrees only if both derive the same
	# order from the same names, so the order has to come from the text.
	# Sorting an array of StringName compares interning pointers instead, which
	# is a per-process accident, and the component divergence hash cannot catch
	# the skew because both peers hold the same name set and differ only in
	# order. The result is a value silently applied to the wrong property.
	var script := node.get_script() as Script
	var table: Array = NetwScriptModel._cache_for(script).sorted_properties()
	var by_text := PackedStringArray()
	for name_ in table:
		by_text.append(String(name_))
	var shuffled := PackedStringArray(by_text)
	shuffled.sort()
	assert_array(Array(by_text)).override_failure_message(
		"property ids must be ordered by name text, not by interning order",
	).is_equal(Array(shuffled))
	for i in table.size():
		assert_int(NetwScriptModel.get_property_id(script, table[i])) \
				.is_equal(i + 1)
		assert_str(
			String(
				NetwScriptModel.get_property_name_by_id(script, i + 1),
			),
		).is_equal(String(table[i]))


func _prop_datagram(seq: int, route: int, property: StringName, value: Variant) -> PackedByteArray:
	var frame := NetwFrameEnvelope.pack(
		route,
		0,
		NetwFrameEnvelope.Channel.PROPERTY_SYNC,
		_serialize_prop(property, value),
	)
	var packet := PackedByteArray()
	packet.resize(3)
	packet[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE
	packet.encode_u16(1, seq)
	packet.append_array(frame)
	return packet


func test_txn_book_disconnect_sweep() -> void:
	var node = TestTargetNode.new()
	_bound_entity(1, node)
	var promise: NetwPromise = api._rpc_core.request_call(
		2,
		node.request_val,
		[1],
		99999,
	)

	# Simulate peer 2 disconnecting
	api._clear_disconnected_peer(2)

	assert_that(promise.is_completed).is_false()
	assert_that(promise.is_failed).is_true()
	assert_that(promise.code).is_equal(ERR_UNAVAILABLE)


func test_path_traversal_rejected() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	Netw.configure_rpc(node.greet)

	# Pack an envelope targeting path traversal (comp = 255, path = "../../sneaky")
	var call_payload := _pack_call(0, &"greet", ["hello", 42])
	var bytes := NetwFrameEnvelope.pack(1, 255, NetwFrameEnvelope.Channel.CALL, call_payload, "../../sneaky")

	var received := []
	node.greeted.connect(func(msg: String) -> void: received.append(msg))

	# Dispatch dynamic envelope
	api._drive_carrier(0, bytes, true)

	# Since it is traversal, it should be rejected and NOT executed (greeted is not emitted)
	assert_that(received.size()).is_equal(0)


func test_rpc_authority_validation() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	Netw.configure_rpc(node.admin_action)

	# Target method admin_action is annotated with @rpc("authority", ...)
	var call_payload := _pack_call(0, &"admin_action", [])

	# 1. Authority caller (sender = 1, since 1 is the default multiplayer authority)
	api._drive_carrier(
		1,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.CALL,
			call_payload,
		),
		true,
	)
	assert_that(node.admin_action_called).is_true()

	# Reset flag
	node.admin_action_called = false

	# 2. Non-authority caller (sender = 2)
	api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.CALL,
			call_payload,
		),
		true,
	)
	assert_that(node.admin_action_called).is_false()


func test_relay_sender_visible_to_handler() -> void:
	# A handler reached through the carrier reads the frame's sender through
	# get_remote_sender_id, the same value a native @rpc handler would see.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	Netw.configure_rpc(node.record_sender)

	var call_payload := _pack_call(0, &"record_sender", [])
	api._drive_carrier(
		7,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.CALL,
			call_payload,
		),
		true,
	)

	assert_that(node.last_sender).is_equal(7)


func test_relay_sender_clears_after_dispatch() -> void:
	# Outside a relayed dispatch the sender falls back to the inner API, so the
	# stamp never leaks past the frame it belongs to.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	Netw.configure_rpc(node.record_sender)

	var call_payload := _pack_call(0, &"record_sender", [])
	api._drive_carrier(
		7,
		NetwFrameEnvelope.pack(
			1,
			0,
			NetwFrameEnvelope.Channel.CALL,
			call_payload,
		),
		true,
	)

	assert_that(api._relay_sender).is_equal(0)


func test_native_rpc_on_entity_intercepted() -> void:
	# A native rpc_id on a node inside a live entity is upgraded to a
	# route-addressed carrier CALL instead of a NodePath-addressed native RPC.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)

	node.greet.rpc_id(2, "hi", 1)

	assert_that(api.sent_packets.size()).is_equal(1)
	var frames = NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	assert_that(frames.size()).is_equal(1)
	assert_that(frames[0]["channel"]).is_equal(NetwFrameEnvelope.Channel.CALL)
	assert_that(frames[0]["route"]).is_equal(1)


func test_quantized_call_body_roundtrip() -> void:
	var quantizers := [
		NetwQuantizeBits.new().bits(12).limits(-180.0, 180.0),
		NetwQuantizeBits.new().bits(8).limits(-1.0, 1.0),
		null,
	]
	var arg_types := [TYPE_FLOAT, TYPE_VECTOR2, TYPE_INT]
	var args := [90.0, Vector2(0.5, -0.5), 7]

	var w := NetwBitBufferWriter.new()
	NetwScriptModel.write_call_body(w, 3, args, quantizers, arg_types)
	var bytes := w.to_bytes()

	# The quantized body is far smaller than the tagged/var_to_bytes form.
	var tagged := var_to_bytes([3, args])
	assert_that(bytes.size()).is_less(tagged.size())

	var r := NetwBitBufferReader.create(bytes)
	assert_that(NetwScriptModel.read_method_token(r)).is_equal(3)
	var decoded := NetwScriptModel.read_call_args(r, quantizers, arg_types)
	assert_that(decoded.size()).is_equal(3)
	assert_float(decoded[0]).is_equal_approx(90.0, 0.2)
	assert_vector(decoded[1]).is_equal_approx(Vector2(0.5, -0.5), Vector2(0.02, 0.02))
	assert_that(decoded[2]).is_equal(7)


func test_quantized_args_dispatch_through_relay() -> void:
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	Netw.configure_rpc(node.aim).quantize(
		[
			NetwQuantizeBits.new().bits(12).limits(-180.0, 180.0),
			NetwQuantizeBits.new().bits(8).limits(-1.0, 1.0),
		],
	)

	# Local dispatch runs the method in-process through the production path.
	Netw.rpc(node.aim, [90.0, Vector2(0.5, -0.5)])

	assert_float(node.last_yaw).is_equal_approx(90.0, 0.2)
	assert_vector(node.last_dir).is_equal_approx(Vector2(0.5, -0.5), Vector2(0.02, 0.02))


static func _pack_call(txn_id: int, method_val: Variant, args: Array) -> PackedByteArray:
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, txn_id)
	# No quantizers or arg types: every argument rides the tagged path.
	NetwScriptModel.write_call_body(w, method_val, args, [], [])
	return w.to_bytes()


func test_emit_entity_signal() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	# Connect to greeted signal
	var received := []
	node.greeted.connect(func(msg: String) -> void: received.append(msg))

	# Configure signal policy
	Netw.configure_signal(node.greeted).any_peer()

	# Emit entity signal locally (this broadcasts to the server/peers)
	Netw.emit_entity_signal(node.greeted, ["hello"])

	# Since it's sent to local target 1, it dispatches locally and triggers signal emission on the receiver!
	assert_that(received.size()).is_equal(1)
	assert_that(received[0]).is_equal("hello")


func test_custom_netw_channel() -> void:
	var cmd := Netw.channel(mt, 105)

	var received := []
	cmd.register(
		func(sender: int, payload: PackedByteArray) -> void:
			received.append([payload, sender])
	)

	# Send channel payload (from sender = 1)
	cmd.send(1, var_to_bytes("ping"))

	# Since it's sent to local target 1, it dispatches and invokes the callback
	assert_that(received.size()).is_equal(1)
	assert_that(bytes_to_var(received[0][0])).is_equal("ping")
	assert_that(received[0][1]).is_equal(1)


func test_multi_frame_unpack() -> void:
	# Three frames concatenated into one datagram unpack back into three.
	var a := NetwFrameEnvelope.pack(1, 0, NetwFrameEnvelope.Channel.SYNC, var_to_bytes("a"))
	var b := NetwFrameEnvelope.pack(2, 0, NetwFrameEnvelope.Channel.SYNC_DELTA, var_to_bytes("bb"))
	var c := NetwFrameEnvelope.pack(3, 255, NetwFrameEnvelope.Channel.PROPERTY_SYNC, var_to_bytes("ccc"), "Child")

	var datagram := PackedByteArray()
	datagram.append_array(a)
	datagram.append_array(b)
	datagram.append_array(c)

	var frames := NetwFrameEnvelope.unpack_all(datagram)
	assert_that(frames.size()).is_equal(3)
	assert_that(frames[0]["route"]).is_equal(1)
	assert_that(frames[1]["route"]).is_equal(2)
	assert_that(frames[2]["route"]).is_equal(3)
	assert_that(frames[2]["path"]).is_equal("Child")
	assert_that(bytes_to_var(frames[2]["payload"])).is_equal("ccc")


func test_aggregation_batches_frames_into_one_packet() -> void:
	api.object_configuration_add(null, NetwClockConfig.new())
	api._replication.send_to(5, 1, NetwFrameEnvelope.Channel.PROPERTY_SYNC, var_to_bytes([&"a", 1]), false)
	api._replication.send_to(5, 2, NetwFrameEnvelope.Channel.PROPERTY_SYNC, var_to_bytes([&"b", 2]), false)

	# Buffered, nothing on the wire yet.
	assert_that(api.sent_packets.size()).is_equal(0)

	api._replication.flush_all_buffers()
	assert_that(api.sent_packets.size()).is_equal(1)

	var frames := NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	assert_that(frames.size()).is_equal(2)


func test_aggregation_splits_at_mtu() -> void:
	api.object_configuration_add(null, NetwClockConfig.new())
	var big := PackedByteArray()
	big.resize(400)
	for i in 5:
		api._replication.send_to(5, 1, NetwFrameEnvelope.Channel.PROPERTY_SYNC, big, false)
	api._replication.flush_all_buffers()

	# Five ~405-byte frames exceed the 1200-byte unreliable cap, so at least one
	# mid-stream flush happened before the final flush.
	assert_that(api.sent_packets.size()).is_greater_equal(2)


func test_quantizer_validation() -> void:
	var node = TestTargetNode.new()
	auto_free(node)

	# Valid property quantization configuration
	var valid_config := Netw.configure_property(node, "health")
	assert_that(valid_config._validate_quantizer_support()).is_true()

	# Configure it with a valid quantizer
	valid_config.quantize(NetwQuantizeBits.new().bits(8).limits(0.0, 100.0))

	# Configure an invalid quantizer (greeted signal expects String, NetwQuantizeBits doesn't support String)
	var invalid_signal_config := Netw.configure_signal(node.greeted)
	invalid_signal_config.quantizers = [NetwQuantizeBits.new().bits(8).limits(0.0, 100.0)]
	assert_that(invalid_signal_config._validate_quantizer_support()).is_false()


# A component address arrives off the wire and names its own comp, so an entity
# holding no owner node is reachable from a remote peer rather than only from a
# local mistake. Every branch of the resolver traverses from the owner, so a
# nodeless route has to be refused before any of them run.
func test_component_address_at_a_nodeless_route_resolves_to_nothing() -> void:
	var nodeless := NetwEntity.new()
	assert_object(nodeless.owner).is_null()

	var replication := api._replication
	# comp 0 is the entity root, 255 is the relative-path fallback, and anything
	# between indexes the component table. None of the three has anywhere to look.
	assert_object(replication.resolve_comp_node(nodeless, 0, "")).is_null()
	assert_object(replication.resolve_comp_node(nodeless, 255, "Child")).is_null()
	assert_object(replication.resolve_comp_node(nodeless, 3, "")).is_null()
	# A null entity is the same answer, because the receive dispatch reaches here
	# with whatever the route table gave it.
	assert_object(replication.resolve_comp_node(null, 255, "Child")).is_null()


# A path shaped safely can still land outside the subtree, because a unique
# name is resolved against the whole owning scene rather than against the node
# it is asked from. The shape check happens before anything resolves, so the
# containment answer can only come from the node that came back.
func test_a_relative_component_address_may_not_leave_the_entity_subtree() -> void:
	var root := Node.new()
	root.name = "Root"
	add_child(root)
	auto_free(root)

	var outsider := Node.new()
	outsider.name = "Outsider"
	root.add_child(outsider)
	outsider.owner = root
	outsider.unique_name_in_owner = true

	var host := Node.new()
	host.name = "Host"
	root.add_child(host)
	host.owner = root

	var inside := Node.new()
	inside.name = "Inside"
	host.add_child(inside)
	inside.owner = root

	var entity := NetwEntity.ensure(host)
	var replication := api._replication

	assert_bool(NetwCompTable.path_shape_is_safe("%Outsider")).is_true()
	assert_object(root.get_node_or_null("%Outsider")).is_not_null()

	assert_that(replication.resolve_comp_node(entity, 0, "")).is_equal(host)
	assert_that(replication.resolve_comp_node(entity, 255, "Inside")) \
			.is_equal(inside)
	assert_that(replication.resolve_comp_node(entity, 255, ".")).is_equal(host)
	assert_object(replication.resolve_comp_node(entity, 255, "%Outsider")) \
			.is_null()
