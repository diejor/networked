## Options bag for [method EntityComponent.despawn].
##
## Carries the knobs that control teardown behavior. Built as a
## [RefCounted] so future options (e.g., delayed-free for death
## animations) can be added without breaking call sites.
class_name DespawnOpts
extends RefCounted

## Recorded on the despawn span and forwarded to the
## [signal EntityComponent.despawning] signal so user code can branch
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


func _init(p_reason: StringName = &"") -> void:
	reason = p_reason
