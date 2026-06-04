## Unit tests for [AsyncMutex] coroutine-safe locking.
class_name TestAsyncMutex
extends NetwTestSuite

var mutex: AsyncMutex


func before_test() -> void:
	mutex = AsyncMutex.new()


func test_lock_and_unlock_updates_state() -> void:
	await mutex.lock()
	assert_that(mutex.is_locked()).is_true()

	mutex.unlock()
	assert_that(mutex.is_locked()).is_false()


func test_unlock_releases_and_is_idempotent() -> void:
	var emitted := [false]
	mutex.released.connect(func() -> void: emitted[0] = true, CONNECT_ONE_SHOT)
	await mutex.lock()
	mutex.unlock()
	assert_that(emitted[0]).is_true()
	mutex.unlock()
	assert_that(mutex.is_locked()).is_false()

	mutex.unlock()
	assert_that(mutex.is_locked()).is_false()


func test_second_lock_waits_until_release() -> void:
	await mutex.lock()

	var second_acquired := [false]
	# Start a second lock attempt. It should block.
	var acquire_second := func() -> void:
		await mutex.lock()
		second_acquired[0] = true
	acquire_second.call()

	# Give the coroutine a frame to start waiting
	await get_tree().process_frame
	assert_that(second_acquired[0]).is_false()

	# Release the first lock. The second should acquire.
	mutex.unlock()
	await wait_until(func(): return second_acquired[0])
	assert_that(second_acquired[0]).is_true()
	assert_that(mutex.is_locked()).is_true()

	mutex.unlock()
