## Value result of decoding one sync frame before shell application.
class_name NetwStagedWrites
extends RefCounted

## Wire ordinal decoded from the frame.
var ordinal: int

## Authoring tick, or [code]-1[/code] for an unstamped frame.
var tick: int = -1

## Reconciliation acknowledgement, or [code]-1[/code].
var ack: int = -1

## Ordered property keys selected by the frame.
var keys: Array[StringName] = []

## Values parallel to [member keys].
var values: Array = []

## Complete reconstructed row after masked-frame merging.
var row: Dictionary = { }

## Whether this frame itself carried every volatile field.
var whole := true

## Redundant window rows as [code]{tick, payload}[/code] records.
var samples: Array = []

## Taped input epoch, or [code]-1[/code].
var tape_epoch: int = -1

## Taped transition entries.
var entries: Array = []


## Returns whether the decoded frame has a consistent key and value shape.
func is_valid() -> bool:
	return not keys.is_empty() and keys.size() == values.size()


## Returns the compatibility header consumed by prediction and display feeds.
func header() -> Dictionary:
	return {
		&"ordinal": ordinal,
		&"tick": tick,
		&"ack": ack,
		&"payload": row,
		&"whole": whole,
		&"samples": samples,
		&"tape_epoch": tape_epoch,
		&"entries": entries,
	}
