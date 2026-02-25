class_name AsyncMutex
extends RefCounted

signal released

var _held := false

func lock() -> void:
	while _held:
		await released
	_held = true

func unlock() -> void:
	if not _held:
		return
	_held = false
	released.emit()

func is_locked() -> bool:
	return _held
