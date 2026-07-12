## Wire-first tests for the acked unreliable datagram shape, the third carrier
## magic that echoes the freshest inbound sequence back to its sender.
##
## The datagram-level state-ack lands with the unified sync convergence, so
## these cases hand-build the acked header and assert its byte layout the way
## the future ingress must parse it: the magic names the shape, a freshness
## [code]u16[/code] and an echo [code]u16[/code] precede the frames. The two
## landed magics still decode unchanged, a short datagram is discarded, and the
## three magics never collide.
class_name TestAckedDatagramMagic
extends NetwTestSuite

class AckTargetNode:
	extends Node

	var health := 100


var mt: MultiplayerTree
var api: NetwMultiplayer
var liveness: NetwLivenessInterface


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "AckMagicTree"
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


func _prop_frame(route: int, property: StringName, value: Variant) -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_token(w, property)
	NetwScriptModel.write_values(w, [value], [], [typeof(value)])
	return NetwFrameEnvelope.pack(
		route, 0, NetwFrameEnvelope.Channel.PROPERTY_SYNC, w.to_bytes()
	)


func test_existing_unreliable_magic_decodes_unchanged() -> void:
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	var packet := PackedByteArray()
	packet.resize(3)
	packet[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE
	packet.encode_u16(1, 5)
	packet.append_array(_prop_frame(7, &"health", 42))
	api._on_inner_peer_packet(1, packet)

	assert_int(node.health).is_equal(42)


func test_existing_reliable_magic_decodes_unchanged() -> void:
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	var packet := PackedByteArray([NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE])
	packet.append_array(_prop_frame(7, &"health", 71))
	api._on_inner_peer_packet(1, packet)

	assert_int(node.health).is_equal(71)


func test_short_unreliable_packet_discarded() -> void:
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	# A datagram too short to carry its freshness u16 is not valid framing.
	api._on_inner_peer_packet(1, PackedByteArray([
		NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE, 0x01,
	]))

	assert_int(node.health).is_equal(100)


func test_acked_datagram_layout_pins_seq_then_echo_then_frame() -> void:
	# The acked shape prepends the magic, the datagram freshness u16, and the
	# echo u16 of the freshest inbound seq, then the aggregated frames.
	var frame := _prop_frame(7, &"health", 88)
	var packet := PackedByteArray()
	packet.resize(5)
	packet[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED
	packet.encode_u16(1, 1234) # this datagram's freshness stamp
	packet.encode_u16(3, 900)  # echoed freshest inbound seq from the destination
	packet.append_array(frame)

	assert_int(packet[0]).is_equal(NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED)
	assert_int(packet.decode_u16(1)).is_equal(1234)
	assert_int(packet.decode_u16(3)).is_equal(900)
	assert_array(packet.slice(5)).is_equal(frame)


# Builds an acked datagram exactly as the egress stamps it.
func _acked_datagram(seq: int, echo: int, frames: Array) -> PackedByteArray:
	var packet := PackedByteArray()
	packet.resize(5)
	packet[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED
	packet.encode_u16(1, seq)
	packet.encode_u16(3, echo)
	for f: PackedByteArray in frames:
		packet.append_array(f)
	return packet


func test_acked_ingress_applies_frame_after_the_header() -> void:
	# The ingress reads the frames past the seq and echo and applies them like
	# any unreliable datagram.
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	api._on_inner_peer_packet(1, _acked_datagram(3, 0, [_prop_frame(7, &"health", 55)]))

	assert_int(node.health).is_equal(55)


func test_acked_ingress_records_the_echo_as_the_peers_state_ack() -> void:
	# The echoed seq is the sender's proof that this peer holds that datagram, so
	# the ingress records it as the peer's confirmed baseline seq.
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	assert_int(api.peer_state_ack(1)).is_equal(-1)
	api._on_inner_peer_packet(1, _acked_datagram(4, 900, [_prop_frame(7, &"health", 55)]))
	assert_int(api.peer_state_ack(1)).is_equal(900)

	# A newer echo advances the confirmed seq, a reordered older one never rolls
	# it back.
	api._on_inner_peer_packet(1, _acked_datagram(5, 950, [_prop_frame(7, &"health", 56)]))
	assert_int(api.peer_state_ack(1)).is_equal(950)
	api._on_inner_peer_packet(1, _acked_datagram(6, 200, [_prop_frame(7, &"health", 57)]))
	assert_int(api.peer_state_ack(1)).is_equal(950)


func test_acked_ingress_gates_freshness_on_the_datagram_seq() -> void:
	# The freshness u16 sits where it does on the plain shape, so a stale acked
	# datagram loses its frame to the per-stream gate exactly like a plain one.
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	api._on_inner_peer_packet(1, _acked_datagram(10, 0, [_prop_frame(7, &"health", 80)]))
	assert_int(node.health).is_equal(80)
	# An older datagram is dropped whole, the value never rewinds.
	api._on_inner_peer_packet(1, _acked_datagram(3, 0, [_prop_frame(7, &"health", 40)]))
	assert_int(node.health).is_equal(80)


func test_short_acked_packet_discarded() -> void:
	# A datagram too short to carry both the freshness and echo u16 is not valid
	# acked framing.
	var node := AckTargetNode.new()
	_bound_entity(7, node)

	api._on_inner_peer_packet(1, PackedByteArray([
		NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED, 0x01, 0x00, 0x02,
	]))

	assert_int(node.health).is_equal(100)


func test_carrier_magics_are_distinct() -> void:
	# Each shape is a distinct leading byte, so the dispatch loop selects exactly
	# one handler from the magic without ambiguity.
	var magics := [
		NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE,
		NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE,
		NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED,
	]
	var seen: Dictionary = { }
	for m: int in magics:
		seen[m] = true
	assert_int(seen.size()).is_equal(magics.size())
