## Unit tests for [HistoryBuffer].
##
## Contract examples cover record/retrieve, bracketing search, and eviction
## under a fixed capacity. The fuzz-driven oracle tests cross-check those
## same invariants against a brute-force dictionary model.
class_name TestHistoryBuffer
extends NetwTestSuite


#region Contract examples

func test_empty_buffer_state() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.is_empty()).is_true()
	assert_that(buf.size()).is_equal(0)
	assert_that(buf.oldest_tick()).is_equal(-1)
	assert_that(buf.newest_tick()).is_equal(-1)
	assert_that(buf.get_at(0)).is_null()
	assert_that(buf.get_at(123)).is_null()
	assert_that(buf.has_tick_after(0)).is_false()


@warning_ignore("unused_parameter")
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


@warning_ignore("unused_parameter")
func test_bracketing_ticks(
	recorded: Array,
	query: int,
	expected: Vector2i,
	test_parameters := [
		# empty buffer -> (-1, -1) regardless of query
		[[], 10, Vector2i(-1, -1)],
		# exact match -> prev is the matched tick, next is following tick
		[[10, 20], 10, Vector2i(10, 20)],
		# query strictly between -> tight bracket
		[[10, 20], 15, Vector2i(10, 20)],
		# query before everything -> prev=-1
		[[10], 5, Vector2i(-1, 10)],
		# query past everything -> next=-1
		[[10], 15, Vector2i(10, -1)],
		# multi-entry query inside -> tight bracket
		[[5, 10, 20, 30], 17, Vector2i(10, 20)],
	],
) -> void:
	var buf := HistoryBuffer.new(8)
	for t: int in recorded:
		buf.record(t, t)
	assert_that(buf.bracketing_ticks(query)).is_equal(expected)


func test_has_tick_after() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	assert_that(buf.has_tick_after(5)).is_true()
	assert_that(buf.has_tick_after(10)).is_false()
	assert_that(buf.has_tick_after(15)).is_false()


#endregion

#region Capacity

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


#endregion

#region Oracle invariants

# fuzz: record monotonic ticks; assert the buffer's view (size, oldest,
# newest, get_at) agrees with an oracle keeping the last N inserts.
@warning_ignore("unused_parameter")
func test_buffer_view_matches_oracle(
	fuzzer := Fuzzers.rangei(1, 1_000_000),
	fuzzer_iterations := 20,
) -> void:
	var capacity := 4
	var insert_count := 12
	var buf := HistoryBuffer.new(capacity)
	var oracle: Array = []

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


# fuzz: record monotonic ticks beyond capacity; assert that every tick
# below the oracle's oldest entry has been evicted.
@warning_ignore("unused_parameter")
func test_evicted_ticks_return_null(
	fuzzer := Fuzzers.rangei(1, 1_000_000),
	fuzzer_iterations := 20,
) -> void:
	var capacity := 4
	var insert_count := 12
	var buf := HistoryBuffer.new(capacity)

	var base_tick: int = fuzzer.next_value()
	for i in insert_count:
		buf.record(base_tick + i, base_tick + i)

	var oldest_kept := base_tick + insert_count - capacity
	for tick in range(base_tick, oldest_kept):
		assert_that(buf.get_at(tick)).is_null()


# fuzz: random bracketing queries against a randomly populated buffer;
# cross-check against a linear-search oracle on the same tick set.
@warning_ignore("unused_parameter")
func test_bracketing_matches_linear_oracle(
	fuzzer := Fuzzers.rangei(0, 50),
	fuzzer_iterations := 20,
) -> void:
	var capacity := 8
	var buf := HistoryBuffer.new(capacity)
	var ticks: PackedInt32Array = []

	var t := 0
	for i in capacity:
		t += 1 + (fuzzer.next_value() % 5)
		buf.record(t, t)
		ticks.append(t)

	var query: int = fuzzer.next_value()
	var expected := Vector2i(-1, -1)
	for stored: int in ticks:
		if stored <= query and stored > expected.x:
			expected.x = stored
		if stored > query and (expected.y == -1 or stored < expected.y):
			expected.y = stored

	assert_that(buf.bracketing_ticks(query)).is_equal(expected)

#endregion
