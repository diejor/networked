## Unit tests for [HistoryBuffer].
##
## [HistoryBuffer] is a pure [RefCounted] with no scene-tree dependencies,
## so every test is self-contained and runs without [code]add_child()[/code].
##
## Coverage areas:
## [ul]
## [li]Basic record and retrieval ([method HistoryBuffer.get_at])[/li]
## [li]Fuzzy lookup ([method HistoryBuffer.get_latest_at_or_before])[/li]
## [li]Ring-buffer eviction when the buffer is full[/li]
## [li]Oldest/newest tick queries[/li]
## [li]History trimming ([method HistoryBuffer.trim_before])[/li]
## [/ul]
class_name TestHistoryBuffer
extends NetworkedTestSuite


# ---------------------------------------------------------------------------
# is_empty
# ---------------------------------------------------------------------------

func test_is_empty_true_on_new_buffer() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.is_empty()).is_true()


func test_is_empty_false_after_record() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(0, "x")
	assert_that(buf.is_empty()).is_false()


# ---------------------------------------------------------------------------
# get_at — exact-tick lookup
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# get_latest_at_or_before — fuzzy lookup
# ---------------------------------------------------------------------------

func test_get_latest_at_or_before_exact_match() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(5, "five")
	assert_that(buf.get_latest_at_or_before(5)).is_equal("five")


func test_get_latest_at_or_before_returns_older_entry() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(3, "three")
	# No entry at tick 7, so returns the one at tick 3.
	assert_that(buf.get_latest_at_or_before(7)).is_equal("three")


func test_get_latest_at_or_before_returns_closest_older_entry() -> void:
	var buf := HistoryBuffer.new(8)
	buf.record(2, "two")
	buf.record(4, "four")
	buf.record(6, "six")
	assert_that(buf.get_latest_at_or_before(5)).is_equal("four")


func test_get_latest_at_or_before_returns_null_when_all_newer() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(10, "ten")
	assert_that(buf.get_latest_at_or_before(5)).is_null()


func test_get_latest_at_or_before_empty_returns_null() -> void:
	var buf := HistoryBuffer.new(4)
	assert_that(buf.get_latest_at_or_before(0)).is_null()


# ---------------------------------------------------------------------------
# oldest_tick / newest_tick
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# Ring-buffer eviction
# ---------------------------------------------------------------------------

func test_eviction_drops_oldest_when_full() -> void:
	# Capacity 3: record ticks 1, 2, 3, then 4 evicts tick 1.
	var buf := HistoryBuffer.new(3)
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(3, "three")
	buf.record(4, "four")  # tick 1 is evicted

	assert_that(buf.get_at(1)).is_null()
	assert_that(buf.get_at(4)).is_equal("four")


func test_eviction_preserves_remaining_entries() -> void:
	var buf := HistoryBuffer.new(3)
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(3, "three")
	buf.record(4, "four")

	assert_that(buf.get_at(2)).is_equal("two")
	assert_that(buf.get_at(3)).is_equal("three")


func test_oldest_tick_updates_after_eviction() -> void:
	var buf := HistoryBuffer.new(2)
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(3, "three")  # evicts tick 1

	assert_that(buf.oldest_tick()).is_equal(2)


func test_full_wrap_around_all_slots() -> void:
	# Record capacity * 2 entries; only the last `capacity` should survive.
	var cap := 4
	var buf := HistoryBuffer.new(cap)
	for i in cap * 2:
		buf.record(i, i * 10)

	for i in cap:
		assert_that(buf.get_at(i)).is_null()  # evicted
	for i in range(cap, cap * 2):
		assert_that(buf.get_at(i)).is_equal(i * 10)


# ---------------------------------------------------------------------------
# trim_before
# ---------------------------------------------------------------------------

func test_trim_before_removes_entries_strictly_before_tick() -> void:
	var buf := HistoryBuffer.new(8)
	buf.record(1, "one")
	buf.record(2, "two")
	buf.record(5, "five")
	buf.trim_before(3)  # removes ticks 1 and 2

	assert_that(buf.get_at(1)).is_null()
	assert_that(buf.get_at(2)).is_null()
	assert_that(buf.get_at(5)).is_equal("five")


func test_trim_before_keeps_entry_at_exact_boundary() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(3, "three")
	buf.trim_before(3)  # trim strictly before 3, so tick 3 survives

	assert_that(buf.get_at(3)).is_equal("three")


func test_trim_before_on_empty_buffer_is_safe() -> void:
	var buf := HistoryBuffer.new(4)
	buf.trim_before(100)  # must not crash
	assert_that(buf.is_empty()).is_true()


func test_trim_before_leaves_buffer_empty_when_all_old() -> void:
	var buf := HistoryBuffer.new(4)
	buf.record(1, "a")
	buf.record(2, "b")
	buf.trim_before(10)

	assert_that(buf.is_empty()).is_true()


func test_oldest_tick_updates_after_trim() -> void:
	var buf := HistoryBuffer.new(8)
	buf.record(1, "a")
	buf.record(3, "b")
	buf.record(5, "c")
	buf.trim_before(4)  # removes ticks 1 and 3

	assert_that(buf.oldest_tick()).is_equal(5)
