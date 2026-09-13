## Test session that extends [NetwMultiplayer] itself and replaces one seam.
##
## A script whose base is the native class contributes no inherited entries to
## its own method list, so what it declares is exactly what it replaced. The
## override adds only [member consumes], so the action a caller sees is exactly
## the one [method NetwMultiplayer.predict_consume] would have answered alone.
extends NetwMultiplayer

## How many times the replaced seam has answered.
var consumes := 0
var display_writes := 0
var gathers := 0
var applies := 0
var acks := 0
var sends := 0
var declares := 0
var undeclares := 0
var constructs := 0


func _predict_consume(depth: int, buffer: int) -> int:
	consumes += 1
	return predict_consume_default(depth, buffer)


func _display_write(_entity: RID, _track: StringName, _value: Variant) -> Error:
	display_writes += 1
	return ERR_SKIP


func _sync_gather_set(_entity: RID, comp: int) -> Array:
	gathers += 1
	return [comp]


func _sync_apply_set(_entity: RID, _comp: int, _values: Array) -> Error:
	applies += 1
	return ERR_SKIP


func _sync_note_ack(_peer: int, _sequence: int) -> void:
	acks += 1


func _sync_note_sent(_peer: int, _sequence: int) -> void:
	sends += 1


func _spawn_declare(_entity: RID, _recipe: Variant) -> Error:
	declares += 1
	return ERR_SKIP


func _spawn_undeclare(_entity: RID) -> void:
	undeclares += 1


func _spawn_construct(_entity: RID) -> Node:
	constructs += 1
	return null
