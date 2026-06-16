## Options bag for [method MultiplayerEntity.despawn].
##
## Carries the knobs that control teardown behavior. Built as a
## [RefCounted] so future options (e.g., delayed-free for death
## animations) can be added without breaking call sites.
class_name DespawnOpts
extends RefCounted

## Recorded on the despawn span and forwarded to the
## [signal MultiplayerEntity.despawning] signal so user code can branch
## on the cause. Common values: [code]&"peer_disconnected"[/code],
## [code]&"killed"[/code], [code]&"collected"[/code],
## [code]&"timeout"[/code].
var reason: StringName

## When [code]true[/code] (default), [SaveComponent.flush] is called
## on the despawning node before authority revert and queue_free. A
## non-OK return is logged at error level and the despawn proceeds -
## from the caller's perspective despawn is infallible.
var flush_save: bool = true

## When [code]true[/code] (default), the [method Node.queue_free] call
## is deferred. This guarantees the engine's next process step sees
## the authority change before the node leaves the tree, which fixes
## the race where a [MultiplayerSynchronizer] tries to push state
## from a freed authority peer.
var defer_free: bool = true

## When [code]true[/code], the entity deactivates now but is freed only after
## [member linger_seconds], so a late shooter can still validate against where it
## was. Its [NetwTimeline] freezes at the despawn boundary and expires when the
## node frees. Default [code]false[/code] keeps the cheap rule: you cannot be shot
## after the server saw you die.
var linger: bool = false

## Seconds a lingering entity stays rewindable before it frees. Sized to the server
## rewind retention window, roughly one second of ticks. Ignored unless
## [member linger] is [code]true[/code].
var linger_seconds: float = 1.0


func _init(p_reason: StringName = &"") -> void:
	reason = p_reason
