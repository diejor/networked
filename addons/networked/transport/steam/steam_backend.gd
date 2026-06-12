## [BackendPeer] implementation using Steam's P2P matchmaking and networking.
##
## Delegates social, lobby discovery, and invite flows to the registered
## [SteamLobbyDirectory] service.
## [codeblock]
## tree.backend = SteamBackend.new()
## var err := await tree.host_player(payload)
## [/codeblock]
@tool
class_name SteamBackend
extends BackendPeer

## Display name for the Steam lobby created by [method create_host_peer].
@export var server_name: String = ""

var _dir: SteamLobbyDirectory


## Resolves the [SteamLobbyDirectory] service for lobby operations.
func setup(tree: MultiplayerTree) -> Error:
	_dir = tree.get_service(SteamLobbyDirectory) as SteamLobbyDirectory
	if _dir == null:
		_dir = tree.get_service(LobbyDirectory) as SteamLobbyDirectory
	if _dir == null:
		Netw.dbg.warn(
			"SteamBackend: SteamLobbyDirectory service not registered.",
		)
		return ERR_UNCONFIGURED
	if not _dir.peer_connect_failed.is_connected(_on_dir_peer_connect_failed):
		_dir.peer_connect_failed.connect(_on_dir_peer_connect_failed)
	return OK


func peer_reset_state() -> void:
	if _dir and _dir.peer_connect_failed.is_connected(_on_dir_peer_connect_failed):
		_dir.peer_connect_failed.disconnect(_on_dir_peer_connect_failed)


func _on_dir_peer_connect_failed(_reason: String) -> void:
	connect_failed.emit(ConnectResult.unreachable(&"STEAM_P2P_FAILED", "Steam peer connection failed."))


## Implements [method BackendPeer.create_host_peer] by creating a Steam lobby.
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("SteamBackend: create_host_peer called.")
	if _dir == null:
		return null
	return await _dir.host_lobby(server_name)


## Implements [method BackendPeer.create_join_peer] with a Steam lobby id.
func create_join_peer(
		_tree: MultiplayerTree,
		server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	Netw.dbg.trace(
		"SteamBackend: create_join_peer called at lobby %s.",
		[server_address],
	)
	if _dir == null:
		return null
	return await _dir.join_lobby_peer(int(server_address))


## Returns the active Steam lobby id for [method MultiplayerTree.join].
func get_join_address() -> String:
	if _dir != null:
		return str(_dir.get_lobby_id())
	return ""


## Steam uses external lobby discovery instead of embedded local hosting.
func supports_embedded_server() -> bool:
	return false


## Implements [method BackendPeer.is_available]. Steam has no web export.
func is_available() -> bool:
	return not OS.has_feature("web")


## Keeps [method BackendPeer.query_server_info] unsupported for saved targets.
##
## Steam lobby browser rows carry [ServerInfo] from
## [method LobbyDirectory.list_lobbies], so this backend does not open a
## separate probe connection.
func query_server_info(
		_address: String,
		_timeout: float = 2.0,
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


## Returns a [code]"Lobby ID"[/code] [AddressHint].
func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Lobby ID",
		"",
		"Steam lobby IDs are discovered through the server browser.",
		false,
		false,
	)


## Preserves authored Steam lobby settings after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	if source is SteamBackend:
		server_name = source.server_name


## Returns the display name for this backend.
func get_display_name() -> String:
	return "Steam"
