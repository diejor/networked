## Test extension that rejects sync frames the stock gate admits.
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
	var verdict := super._sync_admit_frame(
		sender,
		route,
		comp,
		channel,
		flags,
		tick,
		payload,
	)
	return ERR_UNAUTHORIZED if verdict == OK else verdict
