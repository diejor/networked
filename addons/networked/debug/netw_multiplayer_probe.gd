## Worked call-through extension for debugging hostile-input verdicts.
##
## Install this script as the multiplayer implementation, set breakpoints in
## the three overrides, and inspect the returned verdict beside the frame
## header. The stock decision remains authoritative because every override
## delegates to [code]super[/code].
class_name NetwMultiplayerProbe
extends NetwMultiplayer

# Breakpoint door for sync-family frame admission.
func _sync_admit_frame(
		sender: int,
		route: int,
		comp: int,
		channel: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
) -> Error:
	return super._sync_admit_frame(
		sender,
		route,
		comp,
		channel,
		flags,
		tick,
		payload,
	)


# Breakpoint door for spawn-family frame admission.
func _spawn_admit_frame(
		sender: int,
		route: int,
		channel: int,
		payload: PackedByteArray,
) -> Error:
	return super._spawn_admit_frame(sender, route, channel, payload)


# Breakpoint door for prediction-family frame admission.
func _predict_admit_frame(
		sender: int,
		route: int,
		channel: int,
		payload: PackedByteArray,
) -> Error:
	return super._predict_admit_frame(sender, route, channel, payload)
