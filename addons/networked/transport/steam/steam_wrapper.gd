## Internal wrapper for the Steam singleton to provide a clean API.
class_name SteamWrapper


## Mirrors [code]Steam.LobbyType[/code] from the GodotSteam API.
enum LobbyType {
	## Only joinable via invite.
	PRIVATE = 0,
	## Joinable by friends of the host.
	FRIENDS_ONLY = 1,
	## Visible in the lobby browser.
	PUBLIC = 2,
	## Not visible and not joinable (debug only).
	INVISIBLE = 3
}

## Mirrors [code]SteamAPIInitResult[/code] from the Steamworks SDK.
enum InitResult {
	## Steam initialized successfully.
	OK = 0,
	## A non-specific error occurred.
	FAILED_GENERIC = 1,
	## Steam client is not running.
	NO_STEAM_CLIENT = 2,
	## Steam SDK version mismatch.
	VERSION_MISMATCH = 3
}

var _steam: Variant

func _init() -> void:
	if Engine.has_singleton("Steam"):
		_steam = Engine.get_singleton("Steam")

func is_available() -> bool:
	return _steam != null

func steam_init_ex() -> Dictionary:
	return _steam.steamInitEx()

func run_callbacks() -> void:
	_steam.run_callbacks()

func create_lobby(type: int, players: int) -> void:
	_steam.createLobby(type, players)

func join_lobby(lobby_id: int) -> void:
	_steam.joinLobby(lobby_id)

func leave_lobby(lobby_id: int) -> void:
	_steam.leaveLobby(lobby_id)

func get_lobby_owner(lobby_id: int) -> int:
	return _steam.getLobbyOwner(lobby_id)

func set_lobby_data(lobby_id: int, key: String, value: String) -> bool:
	return _steam.setLobbyData(lobby_id, key, value)

## Instantiates a [code]SteamMultiplayerPeer[/code] dynamically to avoid
## parse errors when the GDExtension is missing.
func create_peer() -> MultiplayerPeer:
	if not ClassDB.class_exists("SteamMultiplayerPeer"):
		return null
	return ClassDB.instantiate("SteamMultiplayerPeer") as MultiplayerPeer

## Configures the peer properties using dynamic calls.
func configure_peer(
	peer: MultiplayerPeer,
	nagle: bool,
	relay: bool
) -> void:
	peer.call(&"set_no_delay", not nagle)
	peer.call(&"set_no_nagle", not nagle)
	peer.call(&"set_server_relay", relay)

func get_persona_name() -> String:
	return _steam.getPersonaName()

func connect_signal(sig: String, callable: Callable) -> void:
	_steam.connect(sig, callable)

func disconnect_signal(sig: String, callable: Callable) -> void:
	if _steam.is_connected(sig, callable):
		_steam.disconnect(sig, callable)
