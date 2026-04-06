## Coroutine-safe mutual exclusion primitive for GDScript [code]await[/code] contexts.
##
## Unlike a real OS mutex, this yields via [code]await[/code] instead of blocking the thread.
## [codeblock]
## var mutex := AsyncMutex.new()
## await mutex.lock()
## # critical section
## mutex.unlock()
## [/codeblock]
class_name AsyncMutex
extends RefCounted

signal released

var _held := false

## Acquires the lock, suspending the coroutine until it is available.
func lock() -> void:
	while _held:
		await released
	_held = true

## Releases the lock and wakes one waiting coroutine.
func unlock() -> void:
	if not _held:
		return
	_held = false
	released.emit()

## Returns [code]true[/code] if the lock is currently held.
func is_locked() -> bool:
	return _held
