## Internal wrapper for the Steam singleton to provide a clean API.
##
## All methods are thin proxies over [code]Engine.get_singleton("Steam")[/code].
## Use [method is_available] to check whether the GodotSteam GDExtension is
## present before invoking other methods.
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

## Mirrors [code]Steam.LobbyDistanceFilter[/code].
enum LobbyDistance {
	CLOSE = 0,
	DEFAULT = 1,
	FAR = 2,
	WORLDWIDE = 3,
}

## Mirrors [code]Steam.LobbyComparison[/code].
enum LobbyComparison {
	EQUAL_LESS = -2,
	LESS = -1,
	EQUAL = 0,
	GREATER = 1,
	EQUAL_GREATER = 2,
	NOT_EQUAL = 3,
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

func get_steam_id() -> int:
	return _steam.getSteamID()

func get_persona_name() -> String:
	return _steam.getPersonaName()

## Returns the display name of [param friend_id], or empty string if unknown.
func get_friend_persona_name(friend_id: int) -> String:
	return _steam.getFriendPersonaName(friend_id)

func create_lobby(type: int, players: int) -> void:
	_steam.createLobby(type, players)

func join_lobby(lobby_id: int) -> void:
	_steam.joinLobby(lobby_id)

func leave_lobby(lobby_id: int) -> void:
	_steam.leaveLobby(lobby_id)

func get_lobby_owner(lobby_id: int) -> int:
	return _steam.getLobbyOwner(lobby_id)

func get_num_lobby_members(lobby_id: int) -> int:
	return _steam.getNumLobbyMembers(lobby_id)

func get_lobby_member_limit(lobby_id: int) -> int:
	return _steam.getLobbyMemberLimit(lobby_id)

func get_lobby_member_by_index(lobby_id: int, index: int) -> int:
	return _steam.getLobbyMemberByIndex(lobby_id, index)

func set_lobby_data(lobby_id: int, key: String, value: String) -> bool:
	return _steam.setLobbyData(lobby_id, key, value)

func get_lobby_data(lobby_id: int, key: String) -> String:
	return _steam.getLobbyData(lobby_id, key)

func set_lobby_joinable(lobby_id: int, joinable: bool) -> bool:
	return _steam.setLobbyJoinable(lobby_id, joinable)

func allow_p2p_packet_relay(allow: bool) -> void:
	_steam.allowP2PPacketRelay(allow)

func request_lobby_list() -> void:
	_steam.requestLobbyList()

func add_request_lobby_list_string_filter(
	key: String, value: String, comp: int = LobbyComparison.EQUAL
) -> void:
	_steam.addRequestLobbyListStringFilter(key, value, comp)

func add_request_lobby_list_distance_filter(distance: int) -> void:
	_steam.addRequestLobbyListDistanceFilter(distance)

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

## Resolves a Godot multiplayer [param peer_id] to a Steam ID via the active
## [code]SteamMultiplayerPeer[/code]. Returns [code]0[/code] if unavailable.
func get_steam_id_from_peer_id(peer: MultiplayerPeer, peer_id: int) -> int:
	if peer == null:
		return 0
	return peer.call(&"get_steam_id_from_peer_id", peer_id)

func connect_signal(sig: String, callable: Callable) -> void:
	_steam.connect(sig, callable)

func disconnect_signal(sig: String, callable: Callable) -> void:
	if _steam.is_connected(sig, callable):
		_steam.disconnect(sig, callable)

## Maps [code]EChatRoomEnterResponse[/code] values into readable strings.
static func chat_room_enter_response_to_string(code: int) -> String:
	match code:
		1: return "Success"
		2: return "Lobby does not exist"
		3: return "Not allowed"
		4: return "Lobby is full"
		5: return "Error"
		6: return "Banned"
		7: return "Limited account"
		8: return "Clan disabled"
		9: return "Community ban"
		10: return "Member blocked you"
		11: return "You blocked a member"
		_: return "Unknown response %d" % code
