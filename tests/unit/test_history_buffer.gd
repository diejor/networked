## Unit tests for [HistoryBuffer].
##
## Covers record/retrieve, bracketing search, eviction, and capacity
## normalization. The fuzz-driven oracle tests cross-check the
## ring-buffer's invariants against a brute-force dictionary model.
class_name TestHistoryBuffer
extends NetwTestSuite


func test_empty_buffer_state() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.is_empty()).is_true()
	assert_that(buf.size()).is_equal(0)
	assert_that(buf.oldest_tick()).is_equal(-1)
	assert_that(buf.newest_tick()).is_equal(-1)
	assert_that(buf.get_at(0)).is_null()
	assert_that(buf.get_at(123)).is_null()
	assert_that(buf.has_tick_after(0)).is_false()


func test_record_then_get_at(
	tick: int,
	value: Variant,
	test_parameters := [
		[0, "string-value"],
		[10, 42],
		[20, Vector2(3.0, 4.0)],
		[1000, {&"k": &"v"}],
	],
) -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(tick, value)

	assert_that(buf.is_empty()).is_false()
	assert_that(buf.size()).is_equal(1)
	assert_that(buf.get_at(tick)).is_equal(value)
	assert_that(buf.get_at(tick + 1)).is_null()
	assert_that(buf.oldest_tick()).is_equal(tick)
	assert_that(buf.newest_tick()).is_equal(tick)


func test_find_bracketing_ticks(
	recorded: Array,
	query: int,
	expected_prev: int,
	expected_next: int,
	test_parameters := [
		# empty buffer -> (-1, -1) regardless of query
		[[], 10, -1, -1],
		# exact match -> prev is the matched tick, next is following tick
		[[10, 20], 10, 10, 20],
		# query strictly between -> tight bracket
		[[10, 20], 15, 10, 20],
		# query before everything -> prev=-1
		[[10], 5, -1, 10],
		# query past everything -> next=-1
		[[10], 15, 10, -1],
		# multi-entry query inside -> tight bracket
		[[5, 10, 20, 30], 17, 10, 20],
	],
) -> void:
	var buf := HistoryBuffer.new(8)
	for t: int in recorded:
		buf.record(t, t)

	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(query, 0, out)
	assert_that(out[0]).is_equal(expected_prev)
	assert_that(out[1]).is_equal(expected_next)


func test_has_tick_after() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	assert_that(buf.has_tick_after(5)).is_true()
	assert_that(buf.has_tick_after(10)).is_false()
	assert_that(buf.has_tick_after(15)).is_false()


func test_capacity_normalized_to_power_of_two() -> void:
	# Requested capacity 3 -> internal capacity 4.
	var buf := HistoryBuffer.new(3)
	for tick in range(1, 5):
		buf.record(tick, "v%d" % tick)

	assert_that(buf.size()).is_equal(4)
	assert_that(buf.get_at(1)).is_equal("v1")

	buf.record(5, "v5")
	assert_that(buf.get_at(1)).is_null()
	assert_that(buf.size()).is_equal(4)


# fuzz: record monotonic ticks with random values; cross-check the
# ring-buffer's view against an oracle keeping the last N inserts.
func test_fuzz_record_matches_oracle(
	fuzzer := Fuzzers.rangei(1, 1_000_000),
	fuzzer_iterations := 20,
) -> void:
	var capacity := 4
	var insert_count := 12
	var buf := HistoryBuffer.new(capacity)
	var oracle: Array = []  # of [tick, value]; trimmed to last `capacity`.

	var base_tick: int = fuzzer.next_value()
	for i in insert_count:
		var tick := base_tick + i
		var value := "value_%d" % tick
		buf.record(tick, value)
		oracle.append([tick, value])
		if oracle.size() > capacity:
			oracle.pop_front()

	assert_that(buf.size()).is_equal(oracle.size())
	assert_that(buf.oldest_tick()).is_equal(oracle.front()[0])
	assert_that(buf.newest_tick()).is_equal(oracle.back()[0])

	for entry: Array in oracle:
		assert_that(buf.get_at(entry[0])).is_equal(entry[1])

	# Anything evicted is gone.
	for i in oracle.front()[0] - base_tick:
		assert_that(buf.get_at(base_tick + i)).is_null()


# fuzz: random bracketing queries against a randomly populated buffer;
# cross-check against a linear-search oracle on the same tick set.
func test_fuzz_bracketing_matches_oracle(
	fuzzer := Fuzzers.rangei(0, 50),
	fuzzer_iterations := 20,
) -> void:
	var capacity := 8
	var buf := HistoryBuffer.new(capacity)
	var ticks: PackedInt32Array = []

	# Build a strictly-increasing tick sequence.
	var t := 0
	for i in capacity:
		t += 1 + (fuzzer.next_value() % 5)
		buf.record(t, t)
		ticks.append(t)

	var query: int = fuzzer.next_value()
	var expected_prev := -1
	var expected_next := -1
	for stored: int in ticks:
		if stored <= query and stored > expected_prev:
			expected_prev = stored
		if stored > query and (expected_next == -1 or stored < expected_next):
			expected_next = stored

	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(query, 0, out)
	assert_that(out[0]).is_equal(expected_prev)
	assert_that(out[1]).is_equal(expected_next)
