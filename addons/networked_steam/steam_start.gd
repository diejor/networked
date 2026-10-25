# Reaches GodotSteam by name, so a script that must survive an export without
# GodotSteam can call it.
#
# GodotSteam's own embedded callbacks run only on frames the window draws, so
# a headless or minimized game would never hear from Steam. The SceneTree runs
# them instead, and its connection is the one record that Steam is pumped.

static var _opening: bool

## Starts Steam once per process with [param app_id], or reads how it started.
## Returns GodotSteam's start result, whose [code]status[/code] is
## [code]0[/code] once Steam runs.
static func start(app_id: int) -> Dictionary:
	if not Engine.has_singleton(&"Steam"):
		return { "status": -1, "verbal": "GodotSteam is not installed." }

	var steam := Engine.get_singleton(&"Steam")
	var result: Dictionary = steam.get_steam_init_result()
	if result.get("status") != 0:
		result = steam.steamInitEx(app_id, false)

	var frames := (Engine.get_main_loop() as SceneTree).process_frame
	var pump := Callable(steam, &"run_callbacks")
	if result.status == 0 and not frames.is_connected(pump):
		frames.connect(pump)
	return result


## Opens a Steam lobby of [param type] for [param limit] users once no other
## lobby is opening, so each caller reads its own answer. Steam's answer names
## no request. Returns [code][result, lobby_id][/code].
static func create_lobby(type: int, limit: int) -> Array:
	var steam := Engine.get_singleton(&"Steam")
	var frames := (Engine.get_main_loop() as SceneTree).process_frame
	while _opening:
		await frames
	_opening = true
	steam.createLobby(type, limit)
	var created: Array = await Signal(steam, &"lobby_created")
	_opening = false
	return created
