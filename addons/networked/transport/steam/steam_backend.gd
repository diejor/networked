## [BackendPeer] implementation using Steam's P2P matchmaking and networking.
##
## Delegates social, lobby discovery, and invite flows to the registered
## [LobbyDirectory] service.
@tool
class_name SteamBackend
extends BackendPeer


## Display name for the created session (set before hosting).
@export var server_name: String = ""

var _dir: SteamLobbyDirectory


func setup(_tree: MultiplayerTree) -> Error:
	_dir = _tree.get_service(SteamLobbyDirectory)
	if _dir == null:
		Netw.dbg.warn(
			"SteamBackend: SteamLobbyDirectory service not registered."
		)
		return ERR_UNCONFIGURED
	return OK


func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("SteamBackend: create_host_peer called.")
	if _dir == null:
		return null
	return await _dir.host_lobby(server_name)


func create_join_peer(
	_tree: MultiplayerTree, server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace(
		"SteamBackend: create_join_peer called at lobby %s.",
		[server_address]
	)
	if _dir == null:
		return null
	return await _dir.join_lobby_peer(int(server_address))


func get_join_address() -> String:
	if _dir != null and _dir.has_method("get_lobby_id"):
		return str(_dir.call("get_lobby_id"))
	return ""


func supports_embedded_server() -> bool:
	return false


func query_server_info(
	address: String, timeout: float = 2.0
) -> ServerInfoResult:
	if not address.is_valid_int():
		return ServerInfoResult.error("Invalid Steam lobby ID.")

	var lobby_id := int(address)
	if _dir == null or not _dir.is_inside_tree():
		return ServerInfoResult.error("Steam backend not ready.")

	var wrapper := _dir._wrapper
	if wrapper == null or not wrapper.is_available():
		return ServerInfoResult.error("Steam not available.")

	if not wrapper.request_lobby_data(lobby_id):
		return ServerInfoResult.error("Lobby query failed to send.")

	var result_box := [null]
	var on_data_updated = func(
		success: int, p_lobby_id: int, _member_id: int
	) -> void:
		if p_lobby_id == lobby_id:
			if success == 1:
				var lobby_name := wrapper.get_lobby_data(lobby_id, "name")
				var players := wrapper.get_num_lobby_members(lobby_id)
				var max_players := wrapper.get_lobby_member_limit(lobby_id)
				var info := ServerInfo.new()
				info.motd = lobby_name
				info.players = players
				info.max_players = max_players
				info.metadata = {"lobby_id": str(lobby_id)}
				result_box[0] = ServerInfoResult.ok(info)
			else:
				result_box[0] = ServerInfoResult.error(
					"Lobby does not exist."
				)

	wrapper.lobby_data_update.connect(on_data_updated)

	var time_left := timeout
	var tree := _dir.get_tree()
	while result_box[0] == null and time_left > 0.0:
		if not is_instance_valid(tree):
			break
		await tree.process_frame
		time_left -= tree.get_process_delta_time()

	wrapper.lobby_data_update.disconnect(on_data_updated)

	if result_box[0] != null:
		return result_box[0]
	return ServerInfoResult.timeout()


func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Lobby ID",
		"",
		"Steam lobby IDs are discovered through the server browser.",
		false,
		false
	)


func get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray:
	return []


func copy_from(source: BackendPeer) -> void:
	if source is SteamBackend:
		server_name = source.server_name
