## Laws that pin the masked lane's reconstruction invariant.
##
## The invariant [method NetwSyncPipeline.apply_volatile_frame] states: after an
## accepted masked frame, the receiver's merged row equals the sender's full row
## for that frame's tick, under arbitrary loss, duplication, and reorder, save
## for the rows before the gain edge. It is what lets a correction consume any
## reconstructed row rather than only a whole one. These laws drive the sender
## and receiver bindings directly so loss, duplication, reorder, and the revert
## race are injected exactly rather than sampled, the deterministic counterpart
## of the stochastic loopback run in [TestBroadcastMaskedFlow].
class_name TestMaskedReconstruction
extends NetwTestSuite

const SYNC := NetwFrameEnvelope.Channel.SYNC


# A two-field masked volatile set, the smallest that makes a partial mask mean
# something.
func _masked_set() -> NetwPropertySet:
	var set := NetwPropertySet.new()
	for key: StringName in [&"position", &"rotation"]:
		var field := NetwPropertySet.Column.new(key)
		field.lane = NetwPropertySet.Lane.VOLATILE
		set.bind(field)
	set.masked = true
	return set


func _node() -> Node2D:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	return node


# The first law. Under loss and duplication, every accepted frame reconstructs to
# the sender's staged row for its seq, because the sender diffs against a
# confirmed baseline and so re-carries every field that changed since it. A
# dropped frame strands nothing and a duplicated one merges idempotently.
func test_a_reconstructed_row_equals_the_staged_row_under_loss_and_dup() -> void:
	var set := _masked_set()
	var src := _node()
	var dst := _node()
	var src_binding := NetwPropertySetBinding.new(set, src)
	var dst_binding := NetwPropertySetBinding.new(set, dst)
	var peer := 9

	# Gain edge, delivered and acked, so the baseline advances off the full row.
	var edge := src_binding.masked_delta(0, peer, 100, -1)
	dst_binding.apply_volatile(edge["bytes"])
	src_binding.commit_masked_pending(peer, 1, edge["row"])
	src_binding.advance_masked_ack(peer, 1)

	var seq := 1
	for step in range(1, 8):
		src.position = Vector2(step, 0.0)
		src.rotation = float(step) * 0.25
		seq += 1
		var out := src_binding.masked_delta(0, peer, 100 + step, -1)
		src_binding.commit_masked_pending(peer, seq, out["row"])
		if (out["bytes"] as PackedByteArray).is_empty():
			continue
		if step % 3 == 0:
			continue # drop every third frame
		dst_binding.apply_volatile(out["bytes"])
		if step % 2 == 0:
			dst_binding.apply_volatile(out["bytes"]) # duplicate
		var staged: Dictionary = out["row"]
		assert_vector(dst.position).override_failure_message(
			"an accepted frame must reconstruct to the sender's staged position, "
			+ "or a lost frame stranded a value",
		).is_equal(staged[&"position"])
		assert_float(dst.rotation).override_failure_message(
			"an accepted frame must reconstruct to the sender's staged rotation",
		).is_equal_approx(staged[&"rotation"], 0.0001)


# The second law. The gain edge is always a full row, the anchor every later
# partial merges over. An absent baseline heals the peer with every field.
func test_the_gain_edge_is_always_a_full_mask() -> void:
	var set := _masked_set()
	var src := _node()
	src.position = Vector2(1.0, 2.0)
	src.rotation = 0.5
	var src_binding := NetwPropertySetBinding.new(set, src)

	var edge := src_binding.masked_delta(0, 7, 10, -1)
	assert_bool(edge["full"]).override_failure_message(
		"the first send to an unconfirmed peer must be the full row",
	).is_true()
	var frame := NetwFrameEnvelope.decode_sync_frame(
		edge["bytes"],
		[null, null],
		[TYPE_VECTOR2, TYPE_FLOAT],
	)
	assert_int(int(frame["mask"])).override_failure_message(
		"the gain edge masks in every field, so the mask is all bits set",
	).is_equal((1 << set.columns.size()) - 1)


# The third law. Reorder is absorbed by dropping a stale frame before it ever
# reaches the merge, so a late duplicate of an older seq never rewrites a fresher
# row. Freshest-wins per stream, counted in [code]sync_drops_stale[/code].
func test_a_reordered_frame_is_dropped_not_merged() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	var route := 1
	var sender := 2

	var first := { }
	assert_bool(pipeline.accept_unreliable(sender, route, SYNC, 5, first)) \
			.override_failure_message(
				"the first frame a stream is seen with is always accepted",
			).is_true()
	var fresher := { }
	assert_bool(pipeline.accept_unreliable(sender, route, SYNC, 6, fresher)) \
			.override_failure_message("a fresher seq is accepted").is_true()
	var stale := { }
	assert_bool(pipeline.accept_unreliable(sender, route, SYNC, 5, stale)) \
			.override_failure_message(
				"a reordered older seq must be dropped, not merged over the fresher "
				+ "row it arrived behind",
			).is_false()
	assert_int(int(pipeline.counters()[&"sync_drops_stale"])) \
			.override_failure_message("the dropped stale frame is counted").is_equal(1)


# The fourth law, the revert race that a naive diff strands and the sticky field
# closes. A value moves off the confirmed baseline, is delivered but not acked,
# then reverts to the baseline value before the ack. A plain diff would send
# nothing on the revert and leave the receiver holding the interim value forever.
# The sender keeps the field sticky until the ack, so the revert is re-sent and
# the receiver heals.
func test_the_receiver_heals_from_a_revert() -> void:
	var set := _masked_set()
	var src := _node()
	src.position = Vector2.ZERO
	var dst := _node()
	var src_binding := NetwPropertySetBinding.new(set, src)
	var dst_binding := NetwPropertySetBinding.new(set, dst)
	var peer := 4

	# Gain edge delivered and acked: baseline and receiver both hold the origin.
	var edge := src_binding.masked_delta(0, peer, 10, -1)
	dst_binding.apply_volatile(edge["bytes"])
	src_binding.commit_masked_pending(peer, 1, edge["row"])
	src_binding.advance_masked_ack(peer, 1)

	# The field moves off the baseline, delivered but NOT acked.
	src.position = Vector2(50.0, 0.0)
	var moved := src_binding.masked_delta(0, peer, 11, -1)
	assert_int((moved["bytes"] as PackedByteArray).size()).is_greater(0)
	dst_binding.apply_volatile(moved["bytes"])
	src_binding.commit_masked_pending(peer, 2, moved["row"])
	assert_vector(dst.position).override_failure_message(
		"the receiver must hold the interim value before the revert is tested",
	).is_equal(Vector2(50.0, 0.0))

	# It reverts to the baseline value before the send was acked.
	src.position = Vector2.ZERO
	var reverted := src_binding.masked_delta(0, peer, 12, -1)
	assert_int((reverted["bytes"] as PackedByteArray).size()) \
			.override_failure_message(
				"a field that reverted inside the in-flight window must still be "
				+ "sent, or the receiver strands the interim value",
			).is_greater(0)
	dst_binding.apply_volatile(reverted["bytes"])
	assert_vector(dst.position).override_failure_message(
		"the receiver must heal back to the baseline value the sender reverted to",
	).is_equal(Vector2.ZERO)
