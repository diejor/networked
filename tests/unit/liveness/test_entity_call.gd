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


	@rpc("any_peer", "call_remote", "reliable") func receive_node(n: Variant, value: int) -> void:
		last_node = n


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


# Subclass NetwMultiplayer to mock and capture outgoing packets
class TestNetwMultiplayer:
	extends NetwMultiplayer

	var sent_packets: Array[Dictionary] = []

	# When non-negative, this peer pretends to be that unique id, so a test can
	# exercise the client-side send branches without a live transport.
	var fake_unique_id := -1


	func send_packet(peer_id: int, bytes: PackedByteArray, reliable: bool) -> int:
		_sent_packets += 1
		_sent_bytes += bytes.size()
		sent_packets.append(
			{
				"peer_id": peer_id,
				"bytes": bytes,
				"reliable": reliable,
			},
		)
		return -1


	func _get_unique_id() -> int:
		if fake_unique_id >= 0:
			return fake_unique_id
		return super._get_unique_id()


# Installs the capturing API through the tree's one construction point.
class CaptureTree:
	extends MultiplayerTree

	func _make_api() -> NetwMultiplayer:
		return TestNetwMultiplayer.new(SceneMultiplayer.new(), self)


var mt: MultiplayerTree
var liveness: NetwLivenessInterface
var api: TestNetwMultiplayer


func before_test() -> void:
	mt = CaptureTree.new()
	mt.name = "TestTree"

	add_child(mt)
	auto_free(mt)
	api = mt.api as TestNetwMultiplayer
	liveness = mt.api.liveness


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

	liveness.bind_route(route, entity)
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

	# Call to peer 2 to force it to go to wire/send_packet instead of local dispatch
	Netw.rpc_id(2, node.greet_node, [passenger.owner, 0])

	assert_that(api.sent_packets.size()).is_equal(1)
	var packet = api.sent_packets[0]
	assert_that(packet["peer_id"]).is_equal(2)

	var frames = NetwFrameEnvelope.unpack_all(packet["bytes"])
	assert_that(frames.size()).is_equal(1)

	var env = frames[0]
	assert_that(env["channel"]).is_equal(NetwFrameEnvelope.Channel.CALL)

	# Decode CALL payload: [ flag u8 | target_peer u32 | method token | per-arg stream ]
	var r = NetwBitBuffer.Reader.new(env["payload"])
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
	var r = NetwBitBuffer.Reader.new(frames[0]["payload"])
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

	var ref := NetwNodeRef.new(1, comp_id, "")
	var call_payload := _pack_call(0, &"receive_node", [ref, 7])
	api.replication._dispatch(1, 0, NetwFrameEnvelope.Channel.CALL, call_payload, "", 1)

	assert_that(node.last_node).is_equal(child)


func test_entity_root_argument_crosses_as_root_ref() -> void:
	# An entity root crosses as a NetwNodeRef with component 0, the root address.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	var passenger = _bound_entity(2)

	Netw.rpc_id(3, node.receive_node, [passenger.owner, 0])

	var frames = NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	var r = NetwBitBuffer.Reader.new(frames[0]["payload"])
	var _flag = r.get_aligned_u8()
	var _target_peer = r.get_aligned_u32()
	var _method = NetwScriptModel.read_method_token(r)
	var args: Array = NetwScriptModel.read_call_args(r, [], [])

	var ref = args[0]
	assert_that(ref is NetwNodeRef).is_true()
	assert_that(ref.route).is_equal(2)
	assert_that(ref.comp).is_equal(0)


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
	var promise = NetwPromise.new()
	api.rpc_interface._txn_book.register(7, promise, 2, 99999)

	# Reply to peer 2 so it rides the wire instead of loopback-dispatching.
	api.rpc_interface.send_reply(2, 5, 7, "hello")

	var frames = NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	var env = frames[0]
	assert_that(env["channel"]).is_equal(NetwFrameEnvelope.Channel.REPLY)
	api.replication._dispatch(env["route"], env["comp"], env["channel"], env["payload"], env["path"], 2)

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
	assert_that(p.error).is_equal("Timeout")


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
	mt.api.inner.multiplayer_peer = fake_peer
	mt.role = NetwSessionInterface.Role.CLIENT

	# Trigger client-side hydrated check
	entity._on_identity_hydrated()

	assert_that(entity.components.poisoned).is_true()


# A PROPERTY_SYNC payload is the property token then the value, whatever the
# configured transfer mode. Freshness for an unreliable send rides the carrier
# datagram's sequence stamp, never the payload.
func _serialize_prop(property: StringName, value: Variant) -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_token(w, property)
	NetwScriptModel.write_values(w, [value], [], [typeof(value)])
	return w.to_bytes()


func test_var_synchronization() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	# 1. Authority write (always allowed)
	var payload := _serialize_prop(&"health", 80)
	api.replication._sync_pipeline._property_signal_router.handle_property_sync(entity, node, payload, 1)
	assert_that(node.health).is_equal(80)

	# 2. Non-authority write (default write policy is authority only)
	node.health = 80
	var payload2 := _serialize_prop(&"health", 50)
	api.replication._sync_pipeline._property_signal_router.handle_property_sync(entity, node, payload2, 2)
	assert_that(node.health).is_equal(80)

	# 3. Non-authority write with any_peer policy configured
	Netw.configure_property(node, "health").any_peer().call_local()
	api.replication._sync_pipeline._property_signal_router.handle_property_sync(entity, node, payload2, 2)
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
	api.replication.send_property(node, &"health")
	api.fake_unique_id = -1

	assert_that(api.sent_packets.size()).is_equal(1)
	var sent: Dictionary = api.sent_packets[0]
	assert_that(sent["peer_id"]).is_equal(1)
	assert_that(sent["reliable"]).is_equal(false)

	# The payload is exactly the property token and the value. An unreliable
	# send carries no freshness bytes of its own, because staleness is judged
	# by the carrier datagram's sequence stamp.
	var frame: Dictionary = NetwFrameEnvelope.unpack_all(sent["bytes"])[0]
	var r := NetwBitBuffer.Reader.new(frame["payload"])
	NetwScriptModel.read_token(r)
	var values := NetwScriptModel.read_values(r, [null], [TYPE_INT])
	assert_that(values[0]).is_equal(70)
	assert_that(r.remaining_bytes()).is_equal(0)


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
	var promise = NetwPromise.new()
	api.rpc_interface._txn_book.register(42, promise, 2, 99999)

	# Simulate peer 2 disconnecting
	api._clear_disconnected_peer(2)

	assert_that(promise.is_completed).is_false()
	assert_that(promise.is_failed).is_true()
	assert_that(promise.error).contains("disconnected")


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
	api.replication.receive_carrier(bytes, 0)

	# Since it is traversal, it should be rejected and NOT executed (greeted is not emitted)
	assert_that(received.size()).is_equal(0)


func test_rpc_authority_validation() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)

	Netw.configure_rpc(node.admin_action)

	# Target method admin_action is annotated with @rpc("authority", ...)
	var call_payload := _pack_call(0, &"admin_action", [])

	# 1. Authority caller (sender = 1, since 1 is the default multiplayer authority)
	api.replication._dispatch(1, 0, NetwFrameEnvelope.Channel.CALL, call_payload, "", 1)
	assert_that(node.admin_action_called).is_true()

	# Reset flag
	node.admin_action_called = false

	# 2. Non-authority caller (sender = 2)
	api.replication._dispatch(1, 0, NetwFrameEnvelope.Channel.CALL, call_payload, "", 2)
	assert_that(node.admin_action_called).is_false()


func test_relay_sender_visible_to_handler() -> void:
	# A handler reached through the carrier reads the frame's sender through
	# get_remote_sender_id, the same value a native @rpc handler would see.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	Netw.configure_rpc(node.record_sender)

	var call_payload := _pack_call(0, &"record_sender", [])
	api.replication._dispatch(1, 0, NetwFrameEnvelope.Channel.CALL, call_payload, "", 7)

	assert_that(node.last_sender).is_equal(7)


func test_relay_sender_clears_after_dispatch() -> void:
	# Outside a relayed dispatch the sender falls back to the inner API, so the
	# stamp never leaks past the frame it belongs to.
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	Netw.configure_rpc(node.record_sender)

	var call_payload := _pack_call(0, &"record_sender", [])
	api.replication._dispatch(1, 0, NetwFrameEnvelope.Channel.CALL, call_payload, "", 7)

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

	var w := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_call_body(w, 3, args, quantizers, arg_types)
	var bytes := w.to_bytes()

	# The quantized body is far smaller than the tagged/var_to_bytes form.
	var tagged := var_to_bytes([3, args])
	assert_that(bytes.size()).is_less(tagged.size())

	var r := NetwBitBuffer.Reader.new(bytes)
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
	var w := NetwBitBuffer.Writer.new()
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
	api.clock._configured = true
	api.replication.send_to(5, 1, NetwFrameEnvelope.Channel.PROPERTY_SYNC, var_to_bytes([&"a", 1]), false)
	api.replication.send_to(5, 2, NetwFrameEnvelope.Channel.PROPERTY_SYNC, var_to_bytes([&"b", 2]), false)

	# Buffered, nothing on the wire yet.
	assert_that(api.sent_packets.size()).is_equal(0)

	api.replication.flush_all_buffers()
	assert_that(api.sent_packets.size()).is_equal(1)

	var frames := NetwFrameEnvelope.unpack_all(api.sent_packets[0]["bytes"])
	assert_that(frames.size()).is_equal(2)


func test_aggregation_splits_at_mtu() -> void:
	api.clock._configured = true
	var big := PackedByteArray()
	big.resize(400)
	for i in 5:
		api.replication.send_to(5, 1, NetwFrameEnvelope.Channel.PROPERTY_SYNC, big, false)
	api.replication.flush_all_buffers()

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
