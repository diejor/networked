## Unit tests for [HistoryBuffer].
##
## Exercises record retrieval, bracketing search, and ring-buffer eviction.
class_name TestHistoryBuffer
extends NetworkedTestSuite


func test_is_empty_true_on_new_buffer() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.is_empty()).is_true()
	assert_that(buf.size()).is_equal(0)


func test_is_empty_false_after_record() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(0, "x")
	assert_that(buf.is_empty()).is_false()
	assert_that(buf.size()).is_equal(1)


func test_get_at_returns_null_on_empty_buffer() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.get_at(0)).is_null()


func test_get_at_returns_recorded_value() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "hello")
	assert_that(buf.get_at(10)).is_equal("hello")


func test_get_at_returns_null_for_wrong_tick() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "hello")
	assert_that(buf.get_at(11)).is_null()


func test_get_at_handles_variant_types() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(1, Vector2(3.0, 4.0))
	assert_that(buf.get_at(1)).is_equal(Vector2(3.0, 4.0))


func test_get_at_distinguishes_multiple_ticks() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(1, "a")
	buf.record(2, "b")
	buf.record(3, "c")
	assert_that(buf.get_at(1)).is_equal("a")
	assert_that(buf.get_at(2)).is_equal("b")
	assert_that(buf.get_at(3)).is_equal("c")


func test_find_bracketing_ticks_exact_match() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	buf.record(20, "b")
	
	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(10, 0, out)
	assert_that(out[0]).is_equal(10)
	assert_that(out[1]).is_equal(20)


func test_find_bracketing_ticks_between_values() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	buf.record(20, "b")
	
	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(15, 0, out)
	assert_that(out[0]).is_equal(10)
	assert_that(out[1]).is_equal(20)


func test_find_bracketing_ticks_before_all() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	
	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(5, 0, out)
	assert_that(out[0]).is_equal(-1)
	assert_that(out[1]).is_equal(10)


func test_find_bracketing_ticks_after_all() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	
	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(15, 0, out)
	assert_that(out[0]).is_equal(10)
	assert_that(out[1]).is_equal(-1)


func test_find_bracketing_ticks_empty() -> void:
	var buf := HistoryBuffer.new(4)
	var out := PackedInt32Array([0, 0])
	buf.find_bracketing_ticks(10, 0, out)
	assert_that(out[0]).is_equal(-1)
	assert_that(out[1]).is_equal(-1)


func test_has_tick_after() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "a")
	assert_that(buf.has_tick_after(5)).is_true()
	assert_that(buf.has_tick_after(10)).is_false()
	assert_that(buf.has_tick_after(15)).is_false()


func test_oldest_tick_returns_minus_one_on_empty() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.oldest_tick()).is_equal(-1)


func test_oldest_tick_returns_first_recorded() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(7, "a")
	buf.record(9, "b")
	assert_that(buf.oldest_tick()).is_equal(7)


func test_newest_tick_returns_minus_one_on_empty() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.newest_tick()).is_equal(-1)


func test_newest_tick_returns_last_recorded() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(3, "a")
	buf.record(5, "b")
	assert_that(buf.newest_tick()).is_equal(5)


func test_capacity_is_normalized_to_power_of_two() -> void:
	var buf := HistoryBuffer.new(3)
	# Internal capacity should be 4
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(3, "three")
	buf.record(4, "four")
	
	assert_that(buf.size()).is_equal(4)
	assert_that(buf.get_at(1)).is_equal("one")
	
	buf.record(5, "five") # Now it should evict tick 1
	assert_that(buf.get_at(1)).is_null()
	assert_that(buf.size()).is_equal(4)


func test_eviction_drops_oldest_when_full() -> void:
	var buf := HistoryBuffer.new(2) # Capacity 2 is already power of two
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(3, "three")  # tick 1 is evicted

	assert_that(buf.get_at(1)).is_null()
	assert_that(buf.get_at(3)).is_equal("three")
	assert_that(buf.size()).is_equal(2)


func test_oldest_tick_updates_after_eviction() -> void:
	var buf := HistoryBuffer.new(2)
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(3, "three")  # evicts tick 1

	assert_that(buf.oldest_tick()).is_equal(2)


func test_full_wrap_around_all_slots() -> void:
	var cap := 4
	var buf := HistoryBuffer.new(cap)
	for i in cap * 2:
		buf.record(i, i * 10)

	for i in cap:
		assert_that(buf.get_at(i)).is_null()  # evicted
	for i in range(cap, cap * 2):
		assert_that(buf.get_at(i)).is_equal(i * 10)
