## The freshness gate the declared value plane's receive side sits behind.
##
## A reordered datagram must be dropped before it reaches an apply, because the
## row it would merge onto has already moved past it. The diff and the merge
## themselves are [NetwReplicationSend]'s and are law-covered in the native
## tiers; what only a shell case can hold is that the gate runs first and says
## so in a counter.
class_name TestMaskedReconstruction
extends NetwTestSuite

const SYNC := NetwFrameEnvelope.Channel.SYNC


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


