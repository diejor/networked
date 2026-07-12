## Wire-first tests for unreliable carrier datagram sequencing: the
## [code]u16[/code] stamp after [constant NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE]
## and the freshest-wins acceptance [method NetwSyncPipeline.accept_unreliable]
## applies per sender, route, and channel stream.
##
## Every case hand-builds datagram bytes and drives them through the real
## receive path, so the byte layout these tests pin is the wire contract, not
## an implementation detail. Reliable datagrams and loopback dispatch carry no
## stamp and are never gated, because both are ordered by construction.
class_name TestDatagramSequencing
extends NetwTestSuite

class SeqTargetNode:
	extends Node

	var health := 100
	var mana := 50


var mt: MultiplayerTree
var api: NetwMultiplayer
var liveness: NetwLivenessInterface


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "SeqTestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	liveness = api.liveness


func _bound_entity(route: int, node: Node) -> NetwEntity:
	var entity := NetwEntity.ensure(node)
	mt.add_child(node)
	auto_free(node)
	liveness.bind_route(route, entity)
	return entity


# One PROPERTY_SYNC payload in its reliable form: property token then value,
# no in-payload sequence. Freshness for these tests comes from the datagram.
func _prop_payload(property: StringName, value: Variant) -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_token(w, property)
	NetwScriptModel.write_values(w, [value], [], [typeof(value)])
	return w.to_bytes()


func _prop_frame(route: int, property: StringName, value: Variant) -> PackedByteArray:
	return NetwFrameEnvelope.pack(
		route,
		0,
		NetwFrameEnvelope.Channel.PROPERTY_SYNC,
		_prop_payload(property, value),
	)


# An unreliable carrier datagram exactly as it crosses the raw byte channel:
# magic, little-endian u16 stamp, then the aggregated frames.
func _datagram(seq: int, frames: Array) -> PackedByteArray:
	var packet := PackedByteArray()
	packet.resize(3)
	packet[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE
	packet.encode_u16(1, seq)
	for frame: PackedByteArray in frames:
		packet.append_array(frame)
	return packet


func _deliver(seq: int, frames: Array, sender: int = 1) -> void:
	api._on_inner_peer_packet(sender, _datagram(seq, frames))


func _stale_drops() -> int:
	return int(api.monitor_snapshot().get("sync_drops_stale", 0))


func test_fresh_datagram_applies() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(5, [_prop_frame(7, &"health", 80)])

	assert_that(node.health).is_equal(80)
	assert_that(_stale_drops()).is_equal(0)


func test_stale_datagram_drops() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(5, [_prop_frame(7, &"health", 80)])
	_deliver(3, [_prop_frame(7, &"health", 60)])

	assert_that(node.health).is_equal(80)
	assert_that(_stale_drops()).is_equal(1)

	_deliver(10, [_prop_frame(7, &"health", 42)])
	assert_that(node.health).is_equal(42)


func test_first_datagram_always_accepted() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	# A stream's very first stamp is accepted whatever its value, including 0,
	# so a peer joining mid-count never starts in a dropped state.
	_deliver(0, [_prop_frame(7, &"health", 33)])

	assert_that(node.health).is_equal(33)


func test_half_window_wraparound() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(65535, [_prop_frame(7, &"health", 10)])
	# Wrapping past zero is fresher: the u16 half window keeps counting.
	_deliver(2, [_prop_frame(7, &"health", 20)])
	assert_that(node.health).is_equal(20)

	# Behind by less than half the window is stale even across the wrap.
	_deliver(65534, [_prop_frame(7, &"health", 30)])
	assert_that(node.health).is_equal(20)
	assert_that(_stale_drops()).is_equal(1)


func test_equal_stamp_is_stale() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(5, [_prop_frame(7, &"health", 80)])
	_deliver(5, [_prop_frame(7, &"health", 60)])

	assert_that(node.health).is_equal(80)
	assert_that(_stale_drops()).is_equal(1)


func test_same_datagram_frames_share_acceptance() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	# Two on-demand sends in one tick aggregate into one datagram as two
	# frames of the same stream. Both must apply off the shared stamp.
	_deliver(4, [
		_prop_frame(7, &"health", 70),
		_prop_frame(7, &"mana", 25),
	])

	assert_that(node.health).is_equal(70)
	assert_that(node.mana).is_equal(25)


func test_same_datagram_frames_share_rejection() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(9, [_prop_frame(7, &"health", 70)])
	var drops_before := _stale_drops()

	_deliver(4, [
		_prop_frame(7, &"health", 1),
		_prop_frame(7, &"mana", 2),
	])

	assert_that(node.health).is_equal(70)
	assert_that(node.mana).is_equal(50)
	# The stream's verdict is evaluated once per datagram, so the second frame
	# reuses it without counting another drop.
	assert_that(_stale_drops()).is_equal(drops_before + 1)


func test_duplicate_datagram_drops_whole() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	var packet := _datagram(6, [_prop_frame(7, &"health", 90)])
	api._on_inner_peer_packet(1, packet)
	assert_that(node.health).is_equal(90)

	node.health = 12345
	api._on_inner_peer_packet(1, packet)

	assert_that(node.health).is_equal(12345)
	assert_that(_stale_drops()).is_equal(1)


func test_delayed_datagram_still_applies_untouched_streams() -> void:
	var node_a := SeqTargetNode.new()
	var node_b := SeqTargetNode.new()
	_bound_entity(7, node_a)
	_bound_entity(8, node_b)

	# The book tracks the last accepted stamp per stream, never a per-sender
	# maximum, so a delayed datagram loses only streams that genuinely have
	# fresher state.
	_deliver(6, [_prop_frame(7, &"health", 61)])
	_deliver(5, [
		_prop_frame(7, &"health", 51),
		_prop_frame(8, &"health", 52),
	])

	assert_that(node_a.health).is_equal(61)
	assert_that(node_b.health).is_equal(52)
	assert_that(_stale_drops()).is_equal(1)


func test_streams_are_per_channel() -> void:
	var node := SeqTargetNode.new()
	var entity := _bound_entity(7, node)
	var seen: Array = []
	api.replication.register_channel(
		100,
		func(_entity: NetwEntity, payload: PackedByteArray, _sender: int) -> void:
			seen.append(payload),
	)

	_deliver(6, [_prop_frame(7, &"health", 66)])
	# A different channel on the same route is its own stream, so an older
	# datagram touching only that channel still applies.
	_deliver(5, [NetwFrameEnvelope.pack(7, 0, 100, PackedByteArray([1]))])

	assert_that(node.health).is_equal(66)
	assert_that(seen.size()).is_equal(1)
	assert_that(entity).is_not_null()


func test_user_channel_route_traffic_gated() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)
	var seen: Array = []
	api.replication.register_channel(
		100,
		func(_entity: NetwEntity, payload: PackedByteArray, _sender: int) -> void:
			seen.append(payload),
	)

	_deliver(6, [NetwFrameEnvelope.pack(7, 0, 100, PackedByteArray([1]))])
	_deliver(3, [NetwFrameEnvelope.pack(7, 0, 100, PackedByteArray([2]))])

	assert_that(seen.size()).is_equal(1)
	assert_that(_stale_drops()).is_equal(1)


func test_route0_frames_never_gated() -> void:
	var seen: Array = []
	api.replication.register_channel(
		100,
		func(_entity, payload: PackedByteArray, _sender: int) -> void:
			seen.append(payload),
	)

	# Peer-scoped protocol frames carry their own freshness (clock timestamps,
	# reliable interest relays) and never enter the stream books.
	_deliver(6, [NetwFrameEnvelope.pack(0, 0, 100, PackedByteArray([1]))])
	_deliver(3, [NetwFrameEnvelope.pack(0, 0, 100, PackedByteArray([2]))])

	assert_that(seen.size()).is_equal(2)
	assert_that(_stale_drops()).is_equal(0)


func test_clear_route_resets_acceptance() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(10, [_prop_frame(7, &"health", 70)])
	api.replication.clear_route(7)
	_deliver(2, [_prop_frame(7, &"health", 21)])

	assert_that(node.health).is_equal(21)


func test_clear_peer_resets_acceptance() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(10, [_prop_frame(7, &"health", 70)])
	api.replication.clear_peer(1)
	_deliver(2, [_prop_frame(7, &"health", 21)])

	assert_that(node.health).is_equal(21)


func test_reliable_datagram_never_gated() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(10, [_prop_frame(7, &"health", 70)])

	# Reliable framing carries no stamp: bytes after the magic are frames.
	var packet := PackedByteArray([NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE])
	packet.append_array(_prop_frame(7, &"health", 44))
	api._on_inner_peer_packet(1, packet)

	assert_that(node.health).is_equal(44)
	assert_that(_stale_drops()).is_equal(0)


func test_ungated_dispatch_bypasses_books() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	_deliver(10, [_prop_frame(7, &"health", 70)])

	# Loopback and direct-drive dispatch carry no datagram stamp. They cannot
	# reorder, so they apply regardless of the stream books.
	api.replication.receive_carrier(_prop_frame(7, &"health", 55), 1, false)

	assert_that(node.health).is_equal(55)


func test_short_unreliable_packet_ignored() -> void:
	var node := SeqTargetNode.new()
	_bound_entity(7, node)

	api._on_inner_peer_packet(1, PackedByteArray([
		NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE,
		0x01,
	]))

	assert_that(node.health).is_equal(100)
	assert_that(_stale_drops()).is_equal(0)
