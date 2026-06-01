## Test double for [SteamWrapper] that never touches the real Steam singleton.
##
## Configure lobby data through public fields, then use it anywhere a
## [SteamWrapper] instance is expected.
class_name NetwMockSteamWrapper
extends SteamWrapper

var requested_lobby_id: int = 0
var request_result: bool = true
var lobby_name: String = "Mock Lobby"
var players: int = 2
var max_players: int = 8


## Returns [code]true[/code] so tests can exercise Steam-dependent paths.
func is_available() -> bool:
	return true


## Records [param lobby_id] and optionally emits [signal lobby_data_update].
func request_lobby_data(lobby_id: int) -> bool:
	requested_lobby_id = lobby_id
	if request_result:
		call_deferred("_emit_lobby_data_update", lobby_id)
	return request_result


## Returns configured lobby metadata for [param key].
func get_lobby_data(_lobby_id: int, key: String) -> String:
	return lobby_name if key == "name" else ""


## Returns the configured current player count.
func get_num_lobby_members(_lobby_id: int) -> int:
	return players


## Returns the configured lobby member limit.
func get_lobby_member_limit(_lobby_id: int) -> int:
	return max_players


func _emit_lobby_data_update(lobby_id: int) -> void:
	lobby_data_update.emit(1, lobby_id, 0)
