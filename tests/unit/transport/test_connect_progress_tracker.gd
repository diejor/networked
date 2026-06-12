## Unit tests for [ConnectProgressTracker].
class_name TestConnectProgressTracker
extends NetwTestSuite

func test_ratio_monotonic_and_bounds() -> void:
	var tracker := ConnectProgressTracker.new()
	tracker.start(1000, 10.0)

	var r0 := tracker.ratio(1000)
	var r1 := tracker.ratio(2000)
	var r2 := tracker.ratio(6000)
	var r3 := tracker.ratio(11000)
	var r4 := tracker.ratio(101000)

	assert_float(r0).is_equal(0.0)
	assert_bool(r1 > r0).is_true()
	assert_bool(r2 > r1).is_true()
	assert_bool(r3 > r2).is_true()
	assert_float(r4).is_equal(ConnectProgressTracker.MAX_RATIO)


func test_sample_throttling() -> void:
	var tracker := ConnectProgressTracker.new()
	assert_dict(tracker.poll(Time.get_ticks_msec())).is_empty()

	tracker.start(Time.get_ticks_msec(), 10.0)
	var initial_sample := tracker.set_message("Connecting...", Time.get_ticks_msec())
	assert_dict(initial_sample).is_not_empty()

	var now := Time.get_ticks_msec()
	assert_dict(tracker.poll(now)).is_empty()

	var sample := tracker.poll(now + ConnectProgressTracker.EMIT_INTERVAL_MS + 1)
	assert_dict(sample).is_not_empty()
	assert_str(sample.get("message", "")).is_equal("Connecting...")
	assert_bool(float(sample.get("ratio", -1.0)) >= 0.0).is_true()


func test_force_updates() -> void:
	var tracker := ConnectProgressTracker.new()
	tracker.start(Time.get_ticks_msec(), 10.0)

	var now := Time.get_ticks_msec()
	var sample1 := tracker.set_message("First message", now)
	var sample2 := tracker.set_message("Second message", now)

	assert_dict(sample1).is_not_empty()
	assert_str(sample1.get("message", "")).is_equal("First message")
	assert_dict(sample2).is_not_empty()
	assert_str(sample2.get("message", "")).is_equal("Second message")
