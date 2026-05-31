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
	_dir = _tree.get_service(SteamLobbyDirectory) as SteamLobbyDirectory
	if _dir == null:
		_dir = _tree.get_service(LobbyDirectory) as SteamLobbyDirectory
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
	if _dir != null:
		return str(_dir.get_lobby_id())
	return ""


func supports_embedded_server() -> bool:
	return false


## Lobby status comes from the directory's lobby list (see
## [method LobbyDirectory.list_lobbies]), so a Steam target carries its
## [ServerInfo] without a separate probe. Saved Steam targets stay
## [method ServerInfoResult.unsupported].
func query_server_info(
	_address: String, _timeout: float = 2.0
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Lobby ID",
		"",
		"Steam lobby IDs are discovered through the server browser.",
		false,
		false
	)

func copy_from(source: BackendPeer) -> void:
	if source is SteamBackend:
		server_name = source.server_name


## Returns the user-facing friendly name for this backend.
func get_display_name() -> String:
	return "Steam"
