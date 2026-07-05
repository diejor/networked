## Unit tests for RelayService, Netw facade, transactions, var sync, signals, and component tables.
class_name TestEntityCall
extends NetwTestSuite

# Dynamic test node helper
class TestTargetNode:
	extends Node
	
	signal greeted(msg: String)
	
	var health := 100
	
	@rpc("any_peer", "call_remote", "reliable")
	func greet(msg: String, value: int) -> void:
		greeted.emit(msg + str(value))
		
	@rpc("any_peer", "call_remote", "reliable")
	func greet_node(target_node: Node, value: int) -> void:
		pass
		
	var admin_action_called := false

	@rpc("authority", "call_remote", "reliable")
	func admin_action() -> void:
		admin_action_called = true

	var last_yaw := 0.0
	var last_dir := Vector2.ZERO

	@rpc("any_peer", "call_remote", "unreliable")
	func aim(yaw: float, dir: Vector2) -> void:
		last_yaw = yaw
		last_dir = dir
		
	@rpc("any_peer", "call_remote", "reliable")
	func request_val(x: int) -> int:
		return x * 2
		
	@rpc("any_peer", "call_remote", "reliable")
	func request_async(x: int) -> NetwPromise:
		var p := NetwPromise.new()
		call_deferred("_resolve_promise", p, x * 3)
		return p
		
	func _resolve_promise(p: NetwPromise, val: int) -> void:
		p.resolve(val)


# Subclass RelayService to mock and capture outgoing packets
class TestRelayService:
	extends RelayService
	
	var sent_packets: Array[Dictionary] = []
	
	func _send_packet(peer_id: int, bytes: PackedByteArray, reliable: bool) -> void:
		_sent_packets += 1
		_sent_bytes += bytes.size()
		sent_packets.append({
			"peer_id": peer_id,
			"bytes": bytes,
			"reliable": reliable
		})


var mt: MultiplayerTree
var liveness: LivenessService
var relay: TestRelayService


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	
	relay = TestRelayService.new()
	relay.name = "RelayService"
	mt.add_child(relay)
	
	add_child(mt)
	auto_free(mt)
	liveness = mt.get_service(LivenessService)


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
	node.greeted.connect(func(msg: String) -> void:
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
	
	assert_that(relay.sent_packets.size()).is_equal(1)
	var packet = relay.sent_packets[0]
	assert_that(packet["peer_id"]).is_equal(2)
	
	var frames = RelayService.test_unpack_all_frames(packet["bytes"])
	assert_that(frames.size()).is_equal(1)
	
	var env = frames[0]
	assert_that(env["command"]).is_equal(RelayService.Command.CALL)
	
	# Decode CALL payload: [ flag u8 | target_peer u32 | method token | per-arg stream ]
	var r = NetwBitBuffer.Reader.new(env["payload"])
	var flag = r.get_aligned_u8()
	var target_type = flag & 3
	assert_that(target_type).is_equal(2)
	var target_peer = r.get_aligned_u32()
	assert_that(target_peer).is_equal(2)
	var _method_raw = RelayService._read_method_token(r)
	var args: Array = RelayService._read_call_args(r, [], [])

	# Unpack route from decoded arg
	var route_marker = args[0]
	assert_that(route_marker is Dictionary).is_true()
	assert_that(route_marker.get("__netw_route")).is_equal(2)


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


func test_request_timeout() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	
	# Request to peer 2 so it is tracked under txn book instead of executing locally
	var p = Netw.request_id(2, node.request_val, [1])
	assert_that(p.is_completed).is_false()
	assert_that(p.is_failed).is_false()
	
	# Advance clock/frame count to trigger sweep timeout (default timeout is 150 frames/ticks)
	for i in 151:
		relay._process(0.01)
		
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
	
	entity.hydrate_components()
	
	# Lexicographical sort check: CompA should be 1, CompB should be 2
	assert_that(entity._components_by_id.get(1)).is_equal(NodePath("CompA"))
	assert_that(entity._components_by_id.get(2)).is_equal(NodePath("CompB"))
	assert_that(entity._ids_by_path.get(NodePath("CompA"))).is_equal(1)
	assert_that(entity._ids_by_path.get(NodePath("CompB"))).is_equal(2)
	assert_that(entity._table_poisoned).is_false()
	assert_that(entity._table_hash).is_not_equal(0)


func test_component_table_divergence_guard() -> void:
	var owner_node = Node2D.new()
	var entity = _bound_entity(1, owner_node)
	
	var comp = Node.new()
	comp.name = "CompA"
	owner_node.add_child(comp)
	entity.register_component(comp)
	
	# Hydrate components to assign table hash
	entity.hydrate_components()
	
	# Mock MultiplayerEntity with diverging table hash
	var me = MultiplayerEntity.new()
	me.name = "MultiplayerEntity"
	me.entity_id = "test_entity"
	owner_node.add_child(me)
	me.set_multiplayer_authority(2)
	auto_free(me)
	
	entity.multiplayer_entity = me
	
	# Set a mismatched table hash on MultiplayerEntity
	me._netw_table_hash = 9999
	
	# Trigger client-side hydrated check
	entity._on_identity_hydrated()
	
	assert_that(entity._table_poisoned).is_true()


func test_var_synchronization() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	
	# 1. Authority write (always allowed)
	var payload := var_to_bytes([&"health", 80])
	relay._var_signal_router.handle_var_sync(entity, node, payload, 1)
	assert_that(node.health).is_equal(80)
	
	# 2. Non-authority write (default write policy is authority only)
	node.health = 80
	var payload2 := var_to_bytes([&"health", 50])
	relay._var_signal_router.handle_var_sync(entity, node, payload2, 2)
	assert_that(node.health).is_equal(80)
	
	# 3. Non-authority write with any_peer policy configured
	Netw.configure_var(node, "health").any_peer()
	relay._var_signal_router.handle_var_sync(entity, node, payload2, 2)
	assert_that(node.health).is_equal(50)


func test_txn_book_disconnect_sweep() -> void:
	var promise = NetwPromise.new()
	relay._txn_book.register(42, promise, 2, 99999)
	
	# Simulate peer 2 disconnecting
	relay._on_peer_disconnected(2)
	
	assert_that(promise.is_completed).is_false()
	assert_that(promise.is_failed).is_true()
	assert_that(promise.error).contains("disconnected")


func test_path_traversal_rejected() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	
	Netw.configure_rpc(node.greet)
	
	# Pack an envelope targeting path traversal (comp = 255, path = "../../sneaky")
	var call_payload := _pack_call(0, &"greet", ["hello", 42])
	var bytes := RelayService.test_pack_frame(1, 255, RelayService.Command.CALL, call_payload, "../../sneaky")
	
	var received := []
	node.greeted.connect(func(msg: String) -> void: received.append(msg))
	
	# Dispatch dynamic envelope
	relay._receive_carrier(bytes)
	
	# Since it is traversal, it should be rejected and NOT executed (greeted is not emitted)
	assert_that(received.size()).is_equal(0)


func test_rpc_authority_validation() -> void:
	var node = TestTargetNode.new()
	var entity = _bound_entity(1, node)
	
	Netw.configure_rpc(node.admin_action)
	
	# Target method admin_action is annotated with @rpc("authority", ...)
	var call_payload := _pack_call(0, &"admin_action", [])
	
	# 1. Authority caller (sender = 1, since 1 is the default multiplayer authority)
	relay._dispatch(1, 0, RelayService.Command.CALL, call_payload, "", 1)
	assert_that(node.admin_action_called).is_true()
	
	# Reset flag
	node.admin_action_called = false
	
	# 2. Non-authority caller (sender = 2)
	relay._dispatch(1, 0, RelayService.Command.CALL, call_payload, "", 2)
	assert_that(node.admin_action_called).is_false()


func test_quantized_call_body_roundtrip() -> void:
	var quantizers := [
		NetwQuantizeBits.new().bits(12).limits(-180.0, 180.0),
		NetwQuantizeBits.new().bits(8).limits(-1.0, 1.0),
		null,
	]
	var arg_types := [TYPE_FLOAT, TYPE_VECTOR2, TYPE_INT]
	var args := [90.0, Vector2(0.5, -0.5), 7]

	var w := NetwBitBuffer.Writer.new()
	RelayService._write_call_body(w, 3, args, quantizers, arg_types)
	var bytes := w.to_bytes()

	# The quantized body is far smaller than the tagged/var_to_bytes form.
	var tagged := var_to_bytes([3, args])
	assert_that(bytes.size()).is_less(tagged.size())

	var r := NetwBitBuffer.Reader.new(bytes)
	assert_that(RelayService._read_method_token(r)).is_equal(3)
	var decoded := RelayService._read_call_args(r, quantizers, arg_types)
	assert_that(decoded.size()).is_equal(3)
	assert_float(decoded[0]).is_equal_approx(90.0, 0.2)
	assert_vector(decoded[1]).is_equal_approx(Vector2(0.5, -0.5), Vector2(0.02, 0.02))
	assert_that(decoded[2]).is_equal(7)


func test_quantized_args_dispatch_through_relay() -> void:
	var node = TestTargetNode.new()
	var _entity = _bound_entity(1, node)
	Netw.configure_rpc(node.aim).quantize([
		NetwQuantizeBits.new().bits(12).limits(-180.0, 180.0),
		NetwQuantizeBits.new().bits(8).limits(-1.0, 1.0),
	])

	# Local dispatch runs the method in-process through the production path.
	Netw.rpc(node.aim, [90.0, Vector2(0.5, -0.5)])

	assert_float(node.last_yaw).is_equal_approx(90.0, 0.2)
	assert_vector(node.last_dir).is_equal_approx(Vector2(0.5, -0.5), Vector2(0.02, 0.02))


static func _pack_call(txn_id: int, method_val: Variant, args: Array) -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, txn_id)
	# No quantizers or arg types: every argument rides the tagged path.
	RelayService._write_call_body(w, method_val, args, [], [])
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
	cmd.register(func(sender: int, payload: PackedByteArray) -> void:
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
	var a := RelayService.test_pack_frame(1, 0, RelayService.Command.STATE, var_to_bytes("a"))
	var b := RelayService.test_pack_frame(2, 0, RelayService.Command.INPUT, var_to_bytes("bb"))
	var c := RelayService.test_pack_frame(3, 255, RelayService.Command.VAR_SYNC, var_to_bytes("ccc"), "Child")

	var datagram := PackedByteArray()
	datagram.append_array(a)
	datagram.append_array(b)
	datagram.append_array(c)

	var frames := RelayService.test_unpack_all_frames(datagram)
	assert_that(frames.size()).is_equal(3)
	assert_that(frames[0]["route"]).is_equal(1)
	assert_that(frames[1]["route"]).is_equal(2)
	assert_that(frames[2]["route"]).is_equal(3)
	assert_that(frames[2]["path"]).is_equal("Child")
	assert_that(bytes_to_var(frames[2]["payload"])).is_equal("ccc")


func test_aggregation_batches_frames_into_one_packet() -> void:
	relay._clock_connected = true
	relay.send_to(5, 1, RelayService.Command.VAR_SYNC, var_to_bytes([&"a", 1]), false)
	relay.send_to(5, 2, RelayService.Command.VAR_SYNC, var_to_bytes([&"b", 2]), false)

	# Buffered, nothing on the wire yet.
	assert_that(relay.sent_packets.size()).is_equal(0)

	relay.flush_all_buffers()
	assert_that(relay.sent_packets.size()).is_equal(1)

	var frames := RelayService.test_unpack_all_frames(relay.sent_packets[0]["bytes"])
	assert_that(frames.size()).is_equal(2)


func test_aggregation_splits_at_mtu() -> void:
	relay._clock_connected = true
	var big := PackedByteArray()
	big.resize(400)
	for i in 5:
		relay.send_to(5, 1, RelayService.Command.VAR_SYNC, big, false)
	relay.flush_all_buffers()

	# Five ~405-byte frames exceed the 1200-byte unreliable cap, so at least one
	# mid-stream flush happened before the final flush.
	assert_that(relay.sent_packets.size()).is_greater_equal(2)

