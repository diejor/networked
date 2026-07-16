## Completion handle for an asynchronous scene operation.
class_name NetwScenePromise
extends RefCounted

## Terminal outcome for a scene operation.
enum Result {
	## The requested operation completed successfully.
	OK,
	## Server policy rejected a player request without changing scenes.
	DENIED,
	## The destination or required scene operation was unavailable.
	UNAVAILABLE,
	## A newer request replaced this operation before it completed.
	SUPERSEDED,
	## Server authority did not answer before the request deadline elapsed.
	TIMED_OUT,
}

## Emitted once with the final [enum Result].
signal completed(result: Result)

## Final outcome, or [code]-1[/code] while pending.
var result: int = -1

## Whether this operation has resolved.
var is_completed: bool:
	get:
		return result >= 0


## Resolves this operation once.
func resolve(outcome: Result) -> void:
	if is_completed:
		return
	result = outcome
	completed.emit(outcome)
