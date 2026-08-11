## Test implementation that counts one intake gate and calls through to stock.
##
## The override adds only the count, so the verdict a caller sees is exactly the
## verdict [NetwMultiplayer] would have returned on its own.
extends NetwMultiplayer

var sync_calls := 0


func _sync_admit_frame(
		sender: int,
		route: int,
		comp: int,
		channel: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
) -> Error:
	sync_calls += 1
	return super._sync_admit_frame(
		sender,
		route,
		comp,
		channel,
		flags,
		tick,
		payload,
	)
