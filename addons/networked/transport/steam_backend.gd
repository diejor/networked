@tool
class_name SteamBackend
extends BackendPeer

## [BackendPeer] implementation using Steam lobbies via GodotSteam.
##
## This backend uses Steam's P2P networking and lobby system. It does not
## support embedded servers or direct IP connections.
## [br][br]
## [b]Note:[/b] Requires the GodotSteam GDExtension to be installed and active.
## [br][br]
## A [SteamService] must be present in the scene tree. This backend will
## create one automatically if none is found during [method setup].


@export_group("Lobby Configuration")

## Maximum number of simultaneous client connections allowed by the server.
@export_range(1, 250, 1, "or_greater", "suffix:players") \
var max_clients: int = 32

## The visibility level of the hosted Steam lobby.
@export var lobby_type := SteamWrapper.LobbyType.PUBLIC

## Initial metadata to set on the Steam lobby when hosting.
## [br][br]
## [b]Example:[/b] [code]{"map": "factory", "mode": "deathmatch"}[/code]
@export var lobby_data: Dictionary = {}

## If [code]true[/code], the numeric Lobby ID will be copied to the system
## clipboard when hosting starts successfully.
@export var copy_lobby_id_to_clipboard: bool = true


@export_group("Socket Options")

## If [code]true[/code], Nagle's algorithm is enabled.
## Disable this for low-latency games (shooters, action).
@export var use_nagle: bool = false

## If [code]true[/code], allows Steam to relay traffic through their servers
## if a direct P2P connection cannot be established.
@export var allow_p2p_relay: bool = true

var _wrapper: SteamWrapper
var _service: SteamService
var _lobby_id: int = 0


## Finds or creates a [SteamService] and caches the [SteamWrapper].
func setup(tree: MultiplayerTree) -> Error:
	_service = tree.get_service(SteamService)
	if not _service:
		_service = tree.find_service_node(SteamService)

	if not _service:
		_service = SteamService.new()
		_service.name = &"SteamService"
		tree.add_child(_service)

	_wrapper = _service.get_wrapper()
	if not _wrapper or not _service.is_ready():
		Netw.dbg.error("SteamBackend: Steam is not available.")
		return ERR_CANT_CREATE

	return OK


## Creates a Steam lobby and starts hosting.
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("SteamBackend: create_host_peer called.")
	if not _wrapper or not _service:
		Netw.dbg.error("SteamBackend: not configured.")
		return null

	_wrapper.create_lobby(lobby_type, max_clients)

	var result: Array = await _service.lobby_created
	var connect_lobby: int = result[0]
	var lobby_id: int = result[1]

	if connect_lobby != 1: # 1 is SUCCESS in Steam API
		Netw.dbg.error(
			"SteamBackend: Failed to create lobby: %d", [connect_lobby]
		)
		return null

	_lobby_id = lobby_id

	for key in lobby_data:
		_wrapper.set_lobby_data(lobby_id, str(key), str(lobby_data[key]))

	var peer: MultiplayerPeer = _wrapper.create_peer()
	if not peer:
		Netw.dbg.error(
			"SteamBackend: Failed to instantiate SteamMultiplayerPeer."
		)
		return null

	_wrapper.configure_peer(peer, use_nagle, allow_p2p_relay)
	var err: Error = peer.call(&"host_with_lobby", lobby_id)
	if err != OK:
		Netw.dbg.error(
			"SteamBackend: Failed to host with lobby: %s",
			[error_string(err)]
		)
		return null

	Netw.dbg.info("Steam server ready. Lobby ID: %d", [_lobby_id])
	if copy_lobby_id_to_clipboard:
		DisplayServer.clipboard_set(str(_lobby_id))
		Netw.dbg.info("Lobby ID copied to clipboard.")
	return peer


## Joins a Steam lobby by ID.
func create_join_peer(
	_tree: MultiplayerTree, server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace("SteamBackend: create_join_peer called at %s", [server_address])
	if not _wrapper or not _service:
		Netw.dbg.error("SteamBackend: not configured.")
		return null

	var lobby_id := server_address.to_int()
	if lobby_id == 0:
		Netw.dbg.error(
			"SteamBackend: Invalid lobby ID: %s", [server_address]
		)
		return null

	_wrapper.join_lobby(lobby_id)

	var result: Array = await _service.lobby_joined
	# lobby_id, permissions, locked, response
	var response: int = result[3]

	if response != 1: # 1 is SUCCESS in Steam API
		Netw.dbg.error("SteamBackend: Failed to join lobby: %d", [response])
		return null

	_lobby_id = lobby_id
	var peer: MultiplayerPeer = _wrapper.create_peer()
	if not peer:
		Netw.dbg.error(
			"SteamBackend: Failed to instantiate SteamMultiplayerPeer."
		)
		return null

	_wrapper.configure_peer(peer, use_nagle, allow_p2p_relay)
	var err: Error = peer.call(&"connect_to_lobby", lobby_id)
	if err != OK:
		Netw.dbg.error(
			"SteamBackend: Failed to connect to lobby: %s",
			[error_string(err)]
		)
		return null

	Netw.dbg.info("Steam client connected to lobby %d", [_lobby_id])
	return peer


## Returns [code]false[/code] as Steam relies on an external lobby system.
func supports_embedded_server() -> bool:
	return false


## Returns the current Steam Lobby ID as a string.
func get_join_address() -> String:
	return str(_lobby_id)


## Leaves the lobby and clears the wrapper reference.
func peer_reset_state() -> void:
	if _lobby_id != 0 and _wrapper:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0

	_wrapper = null
	_service = null


func _get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray:
	var warnings := PackedStringArray()
	if not Engine.has_singleton("Steam"):
		warnings.append(
			"GodotSteam singleton not found. Ensure the plugin is active."
		)
	if not ClassDB.class_exists("SteamMultiplayerPeer"):
		warnings.append(
			"SteamMultiplayerPeer class not found. Ensure GodotSteam " +
			"GDExtension is installed."
		)
	return warnings


func _copy_from(_source: BackendPeer) -> void:
	# Keep transient state empty to avoid accidental sharing after duplication.
	_lobby_id = 0
	_wrapper = null
	_service = null
