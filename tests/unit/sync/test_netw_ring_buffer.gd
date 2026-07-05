## Unit tests for [NetwRingBuffer].
##
## Contract examples cover record/retrieve, bracketing search, and eviction
## under a fixed capacity. Oracle tests cross-check the same invariants against
## a brute-force model.
class_name TestNetwRingBuffer
extends NetwTestSuite

func test_basic_state_and_record_lookup() -> void:
	var empty := NetwRingBuffer.new(4)
	assert_that(empty.is_empty()).is_true()
	assert_that(empty.size()).is_equal(0)
	assert_that(empty.oldest_tick()).is_equal(-1)
	assert_that(empty.newest_tick()).is_equal(-1)
	assert_that(empty.get_at(0)).is_null()
	assert_that(empty.get_at(123)).is_null()
	assert_that(empty.has_tick_after(0)).is_false()

	for row in [
		[0, "string-value"],
		[10, 42],
		[20, Vector2(3.0, 4.0)],
		[1000, { &"k": &"v" }],
	]:
		var buf := NetwRingBuffer.new(4)
		buf.record(row[0], row[1])

		assert_that(buf.is_empty()).is_false()
		assert_that(buf.size()).is_equal(1)
		assert_that(buf.get_at(row[0])).is_equal(row[1])
		assert_that(buf.get_at(row[0] + 1)).is_null()
		assert_that(buf.oldest_tick()).is_equal(row[0])
		assert_that(buf.newest_tick()).is_equal(row[0])

	var buf := NetwRingBuffer.new(4)
	buf.record(10, "a")
	assert_that(buf.has_tick_after(5)).is_true()
	assert_that(buf.has_tick_after(10)).is_false()
	assert_that(buf.has_tick_after(15)).is_false()


func test_bracketing_examples() -> void:
	for row in [
		[[], 10, Vector2i(-1, -1)],
		[[10, 20], 10, Vector2i(10, 20)],
		[[10, 20], 15, Vector2i(10, 20)],
		[[10], 5, Vector2i(-1, 10)],
		[[10], 15, Vector2i(10, -1)],
		[[5, 10, 20, 30], 17, Vector2i(10, 20)],
	]:
		var buf := NetwRingBuffer.new(8)
		for t: int in row[0]:
			buf.record(t, t)
		assert_that(buf.bracketing_ticks(row[1])).is_equal(row[2])


func test_capacity_and_eviction() -> void:
	var buf := NetwRingBuffer.new(3)
	for tick in range(1, 5):
		buf.record(tick, "v%d" % tick)

	assert_that(buf.size()).is_equal(4)
	assert_that(buf.get_at(1)).is_equal("v1")

	buf.record(5, "v5")
	assert_that(buf.get_at(1)).is_null()
	assert_that(buf.size()).is_equal(4)

	var capacity := 4
	var insert_count := 12
	buf = NetwRingBuffer.new(capacity)
	var base_tick := 100

	for i in insert_count:
		buf.record(base_tick + i, base_tick + i)

	var oldest_kept := base_tick + insert_count - capacity
	for tick in range(base_tick, oldest_kept):
		assert_that(buf.get_at(tick)).is_null()


@warning_ignore("unused_parameter")
func test_buffer_view_matches_oracle(
		fuzzer := Fuzzers.rangei(1, 1_000_000),
		fuzzer_iterations := 20,
) -> void:
	var capacity := 4
	var insert_count := 12
	var buf := NetwRingBuffer.new(capacity)
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


@warning_ignore("unused_parameter")
func test_bracketing_matches_linear_oracle(
		fuzzer := Fuzzers.rangei(0, 50),
		fuzzer_iterations := 20,
) -> void:
	var capacity := 8
	var buf := NetwRingBuffer.new(capacity)
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
