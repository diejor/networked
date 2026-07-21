## Laws of [NetwPredictJournal], the record every later verification reads.
##
## A journal that silently loses, reorders, or aliases a row would make every
## fingerprint comparison built on it meaningless, so these cases pin the ring
## semantics, the epoch clear, and the column integrity rather than any
## particular engine's use of them.
class_name TestPredictJournal
extends NetwTestSuite

const KIND_FRESH := 1
const KIND_REPEAT := 2


func test_open_and_close_compose_one_row() -> void:
	var journal := NetwPredictJournal.new(8)
	journal.open(3, 41, KIND_FRESH, 111)
	assert_int(journal.row_at(3)[&"post_fp"]).is_equal(0)
	assert_int(journal.row_at(3)[&"flags"]).is_equal(0)
	assert_int(journal.last_closed()).override_failure_message(
		"an open row holds a zero fingerprint, so it is not a frontier anything "
		+ "may acknowledge",
	).is_equal(-1)

	journal.close(3, 222)

	var row := journal.row_at(3)
	assert_int(row[&"transition"]).is_equal(3)
	assert_int(row[&"label"]).is_equal(41)
	assert_int(row[&"kind"]).is_equal(KIND_FRESH)
	assert_int(row[&"c_hash"]).is_equal(111)
	assert_int(row[&"post_fp"]).is_equal(222)
	assert_int(row[&"e_digest"]).is_equal(0)
	assert_int(row[&"domain"]).is_equal(NetwPredictJournal.Domain.IN_DOMAIN)
	assert_int(row[&"flags"]).is_equal(NetwPredictJournal.ROW_CLOSED)
	assert_int(journal.last_closed()).is_equal(3)


func test_ring_evicts_oldest_and_keeps_columns_aligned() -> void:
	var journal := NetwPredictJournal.new(4)
	for k in 6:
		journal.open(k, 100 + k, KIND_FRESH, 200 + k)
		journal.close(k, 300 + k)

	assert_int(journal.size()).is_equal(4)
	assert_array(journal.transitions()).is_equal([2, 3, 4, 5])
	assert_array(journal.labels()).is_equal([102, 103, 104, 105])
	assert_array(journal.c_hashes()).is_equal([202, 203, 204, 205])
	assert_array(journal.post_fps()).is_equal([302, 303, 304, 305])
	assert_dict(journal.row_at(1)).override_failure_message(
		"an evicted transition must not resolve to a live row",
	).is_empty()


func test_columns_stay_aligned_across_a_wrap() -> void:
	var journal := NetwPredictJournal.new(3)
	for k in 5:
		journal.open(k, 10 * k, KIND_REPEAT if k % 2 else KIND_FRESH, k)
		journal.close(k, 1000 + k)

	var transitions := journal.transitions()
	var labels := journal.labels()
	var kinds := journal.kinds()
	var post_fps := journal.post_fps()
	for i in transitions.size():
		var k: int = transitions[i]
		assert_int(labels[i]).is_equal(10 * k)
		assert_int(post_fps[i]).is_equal(1000 + k)
		assert_int(kinds[i]).is_equal(KIND_REPEAT if k % 2 else KIND_FRESH)


func test_mark_ack_replaces_a_prior_verdict() -> void:
	var journal := NetwPredictJournal.new(4)
	journal.open(0, 0, KIND_FRESH, 1)

	journal.mark_ack(0, false)
	var diverged: int = journal.row_at(0)[&"flags"]
	assert_bool(diverged & NetwPredictJournal.ROW_ACKED > 0).is_true()
	assert_bool(diverged & NetwPredictJournal.ROW_DIVERGENT > 0).is_true()
	assert_bool(diverged & NetwPredictJournal.ROW_MATCHED > 0).is_false()

	journal.mark_ack(0, true)
	var matched: int = journal.row_at(0)[&"flags"]
	assert_bool(matched & NetwPredictJournal.ROW_MATCHED > 0).is_true()
	assert_bool(matched & NetwPredictJournal.ROW_DIVERGENT > 0) \
			.override_failure_message(
				"a row must not carry both verdicts at once",
			).is_false()


func test_first_unmatched_names_the_oldest_unverified_transition() -> void:
	var journal := NetwPredictJournal.new(8)
	for k in 4:
		journal.open(k, k, KIND_FRESH, k)
	assert_int(journal.first_unmatched()).is_equal(0)

	journal.mark_ack(0, true)
	journal.mark_ack(1, true)
	assert_int(journal.first_unmatched()).is_equal(2)

	journal.mark_ack(2, false)
	assert_int(journal.first_unmatched()).override_failure_message(
		"a divergent row is unmatched, so it is where recovery rebases",
	).is_equal(2)

	journal.mark_ack(2, true)
	journal.mark_ack(3, true)
	assert_int(journal.first_unmatched()).is_equal(-1)


func test_last_closed_stops_at_the_first_open_row() -> void:
	var journal := NetwPredictJournal.new(8)
	for k in 4:
		journal.open(k, k, KIND_FRESH, k)
	journal.close(0, 900)
	journal.close(1, 901)

	assert_int(journal.last_closed()).is_equal(1)

	# Closing a later row does not advance the frontier past the open one before
	# it, because a frontier is a claim about every transition up to it.
	journal.close(3, 903)
	assert_int(journal.last_closed()).override_failure_message(
		"an acknowledgement claims a run, so its frontier cannot skip a "
		+ "transition whose state was never produced",
	).is_equal(1)

	journal.close(2, 902)
	assert_int(journal.last_closed()).is_equal(3)


func test_a_substituted_row_is_closed_without_having_run() -> void:
	var journal := NetwPredictJournal.new(8)
	journal.open(0, 0, KIND_FRESH, 7)
	journal.mark_substituted(0)

	assert_int(journal.last_closed()).override_failure_message(
		"authority ran nothing for a substituted transition, so its row is "
		+ "final and acknowledgeable without a state to fingerprint",
	).is_equal(0)


func test_clear_adopts_the_epoch_and_drops_every_row() -> void:
	var journal := NetwPredictJournal.new(4)
	journal.open(0, 0, KIND_FRESH, 7)
	journal.open(1, 1, KIND_FRESH, 8)

	journal.clear(5)

	assert_int(journal.epoch()).is_equal(5)
	assert_int(journal.size()).is_equal(0)
	assert_array(journal.transitions()).is_empty()
	assert_dict(journal.row_at(0)).override_failure_message(
		"transitions are injective only within one epoch",
	).is_empty()

	journal.open(0, 90, KIND_FRESH, 9)
	assert_int(journal.row_at(0)[&"label"]).is_equal(90)


func test_close_and_mark_ack_ignore_an_evicted_transition() -> void:
	var journal := NetwPredictJournal.new(2)
	journal.open(0, 0, KIND_FRESH, 1)
	journal.open(1, 1, KIND_FRESH, 2)
	journal.open(2, 2, KIND_FRESH, 3)

	journal.close(0, 999)
	journal.mark_ack(0, true)

	assert_int(journal.size()).is_equal(2)
	assert_array(journal.transitions()).is_equal([1, 2])
	assert_array(journal.post_fps()).override_failure_message(
		"a write for an evicted transition must not land on a live row",
	).is_equal([0, 0])


func test_fnv1a_is_stable_sensitive_and_within_int32() -> void:
	var empty := NetwPredictJournal.fnv1a(PackedByteArray())
	assert_int(empty).is_equal(NetwPredictJournal.fnv1a(PackedByteArray()))

	var bytes := PackedByteArray([1, 2, 3, 4])
	assert_int(NetwPredictJournal.fnv1a(bytes)) \
			.is_equal(NetwPredictJournal.fnv1a(PackedByteArray([1, 2, 3, 4])))
	assert_int(NetwPredictJournal.fnv1a(bytes)) \
			.is_not_equal(NetwPredictJournal.fnv1a(PackedByteArray([1, 2, 4, 3])))
	assert_int(NetwPredictJournal.fnv1a(bytes)) \
			.is_not_equal(NetwPredictJournal.fnv1a(PackedByteArray([1, 2, 3])))

	# The column is PackedInt32Array, so a fingerprint that did not fit would be
	# stored as a different value than it was computed as.
	for n in 64:
		var probe := PackedByteArray()
		for i in n:
			probe.append((i * 37 + n) & 0xFF)
		var h := NetwPredictJournal.fnv1a(probe)
		assert_bool(h >= -2147483648 and h <= 2147483647) \
				.override_failure_message(
					"fnv1a must produce a signed 32-bit value, got %d" % h,
				).is_true()


func test_a_fingerprint_survives_the_int32_column() -> void:
	var journal := NetwPredictJournal.new(64)
	for n in 64:
		var probe := PackedByteArray()
		for i in n:
			probe.append((i * 91 + n) & 0xFF)
		var h := NetwPredictJournal.fnv1a(probe)
		journal.open(n, n, KIND_FRESH, h)
		journal.close(n, h)
		assert_int(journal.row_at(n)[&"post_fp"]).is_equal(h)
		assert_int(journal.row_at(n)[&"c_hash"]).is_equal(h)
