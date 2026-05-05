@tool
class_name SteamBackend
extends BackendPeer

## [BackendPeer] implementation using Steam lobbies via GodotSteam.
##
## This backend uses Steam's P2P networking and lobby system. It does not
## support embedded servers or direct IP connections.
## [br][br]
## [b]Note:[/b] Requires the GodotSteam GDExtension to be installed and active.


## Singleton guard to prevent multiple [MultiplayerTree] instances from using Steam.
static var _active: WeakRef = weakref(null)


@export_group("Lobby Configuration")

## Maximum number of simultaneous client connections allowed by the server.
@export_range(1, 250, 1, "or_greater", "suffix:players") \
var max_clients: int = 32

## The visibility level of the hosted Steam lobby.
@export var lobby_type := SteamWrapper.LobbyType.PUBLIC

## If [code]true[/code], the backend will automatically call
## [method MultiplayerTree.join] when a Steam invite is received.
## [br][br]
## [b]Note:[/b] If disabled, you must connect to
## [signal MultiplayerTree.invite_received] and manually call [method join] 
## or [method connect_player] to handle invitations.
@export var auto_join_on_invite: bool = true

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
var _callbacks: SteamCallbacks
var _lobby_id: int = 0


func _init() -> void:
	super._init()


## Initializes Steam and sets up signal listeners.
func setup(tree: MultiplayerTree) -> Error:
	var active_tree = _active.get_ref()
	if active_tree and active_tree != tree:
		Netw.dbg.error("SteamBackend: Steam is already in use by another tree.")
		return ERR_ALREADY_IN_USE
	
	_wrapper = SteamWrapper.new()
	if not _wrapper.is_available():
		Netw.dbg.error("SteamBackend: Steam singleton not found.")
		return ERR_CANT_CREATE
	
	var init_res: Dictionary = _wrapper.steam_init_ex()
	var status: int = init_res.get("status", 1)
	if status != SteamWrapper.InitResult.OK:
		var error_msg := "Steam init failed: %d" % status
		if status == SteamWrapper.InitResult.NO_STEAM_CLIENT:
			error_msg += " (No Steam Client)"
		Netw.dbg.error("SteamBackend: %s", [error_msg])
		return ERR_CANT_CREATE
	
	_active = weakref(tree)
	
	_callbacks = SteamCallbacks.new(_wrapper, self)
	_callbacks.name = &"SteamCallbacks"
	tree.add_child(_callbacks)
	
	return OK


## Creates a Steam lobby and starts hosting.
func host() -> Error:
	Netw.dbg.trace("SteamBackend: host called.")
	_wrapper.create_lobby(lobby_type, max_clients)
	
	var result: Array = await _callbacks.lobby_created
	var connect_lobby: int = result[0]
	var lobby_id: int = result[1]
	
	if connect_lobby != 1: # 1 is SUCCESS in Steam API
		Netw.dbg.error(
			"SteamBackend: Failed to create lobby: %d", [connect_lobby]
		)
		return ERR_CANT_CREATE
	
	_lobby_id = lobby_id
	
	for key in lobby_data:
		_wrapper.set_lobby_data(lobby_id, str(key), str(lobby_data[key]))
	
	var peer: MultiplayerPeer = _wrapper.create_peer()
	if not peer:
		Netw.dbg.error("SteamBackend: Failed to instantiate SteamMultiplayerPeer.")
		return ERR_CANT_CREATE
	
	_wrapper.configure_peer(peer, use_nagle, allow_p2p_relay)
	var err: Error = peer.call(&"host_with_lobby", lobby_id)
	
	if err == OK:
		api.multiplayer_peer = peer
		Netw.dbg.info("Steam server ready. Lobby ID: %d", [_lobby_id])
		if copy_lobby_id_to_clipboard:
			DisplayServer.clipboard_set(str(_lobby_id))
			Netw.dbg.info("Lobby ID copied to clipboard.")
	else:
		Netw.dbg.error(
			"SteamBackend: Failed to host with lobby: %s",
			[error_string(err)]
		)
	
	return err


## Joins a Steam lobby by ID.
func join(server_address: String, _username: String = "") -> Error:
	Netw.dbg.trace("SteamBackend: join called at %s", [server_address])
	var lobby_id := server_address.to_int()
	if lobby_id == 0:
		Netw.dbg.error("SteamBackend: Invalid lobby ID: %s", [server_address])
		return ERR_INVALID_PARAMETER
	
	_wrapper.join_lobby(lobby_id)
	
	var result: Array = await _callbacks.lobby_joined
	# lobby_id, permissions, locked, response
	var response: int = result[3]
	
	if response != 1: # 1 is SUCCESS in Steam API
		Netw.dbg.error("SteamBackend: Failed to join lobby: %d", [response])
		return ERR_CANT_CONNECT
	
	_lobby_id = lobby_id
	var peer: MultiplayerPeer = _wrapper.create_peer()
	if not peer:
		Netw.dbg.error("SteamBackend: Failed to instantiate SteamMultiplayerPeer.")
		return ERR_CANT_CREATE
	
	_wrapper.configure_peer(peer, use_nagle, allow_p2p_relay)
	var err: Error = peer.call(&"connect_to_lobby", lobby_id)
	
	if err == OK:
		api.multiplayer_peer = peer
		Netw.dbg.info("Steam client connected to lobby %d", [_lobby_id])
	else:
		Netw.dbg.error(
			"SteamBackend: Failed to connect to lobby: %s",
			[error_string(err)]
		)
	
	return err


## Returns [code]false[/code] as Steam relies on an external lobby system.
func supports_embedded_server() -> bool:
	return false


## Returns the current Steam Lobby ID as a string.
func get_join_address() -> String:
	return str(_lobby_id)


## Leaves the lobby, cleans up callbacks, and clears the singleton guard.
func peer_reset_state() -> void:
	if _lobby_id != 0 and _wrapper:
		_wrapper.leave_lobby(_lobby_id)
		_lobby_id = 0
	
	if _callbacks:
		_callbacks.queue_free()
		_callbacks = null
	
	_active = weakref(null)
	super.peer_reset_state()


func _get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray:
	var warnings := PackedStringArray()
	if not Engine.has_singleton("Steam"):
		warnings.append(
			"GodotSteam singleton not found. Ensure the plugin is active."
		)
	if not ClassDB.class_exists("SteamMultiplayerPeer"):
		warnings.append(
			"SteamMultiplayerPeer class not found. Ensure GodotSteam GDExtension is installed."
		)
	return warnings


func _copy_from(_source: BackendPeer) -> void:
	# Keep transient state empty to avoid accidental sharing after duplication.
	_lobby_id = 0
	_wrapper = null
	_callbacks = null


# ---------------------------------------------------------------------------
# Inner Classes
# ---------------------------------------------------------------------------

## Internal wrapper for the Steam singleton to provide a clean API.
class SteamWrapper:
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


## Internal Node that pumps Steam callbacks and handles signals.
class SteamCallbacks extends Node:
	## Emitted when [method Steam.createLobby] completes.
	signal lobby_created(connect_lobby: int, lobby_id: int)
	## Emitted when [method Steam.joinLobby] completes.
	signal lobby_joined(
		lobby_id: int, permissions: int, locked: bool, response: int
	)
	
	var _wrapper: SteamWrapper
	var _backend_ref: WeakRef
	
	func _init(wrapper: SteamWrapper, backend: SteamBackend) -> void:
		_wrapper = wrapper
		_backend_ref = weakref(backend)
	
	func _ready() -> void:
		_wrapper.connect_signal("lobby_created", _on_lobby_created)
		_wrapper.connect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.connect_signal("join_requested", _on_join_requested)
	
	func _exit_tree() -> void:
		_wrapper.disconnect_signal("lobby_created", _on_lobby_created)
		_wrapper.disconnect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.disconnect_signal("join_requested", _on_join_requested)
	
	func _process(_dt: float) -> void:
		_wrapper.run_callbacks()
	
	func _on_lobby_created(connect_lobby: int, lobby_id: int) -> void:
		lobby_created.emit(connect_lobby, lobby_id)
	
	func _on_lobby_joined(
		lobby_id: int, permissions: int, locked: bool, response: int
	) -> void:
		lobby_joined.emit(lobby_id, permissions, locked, response)
	
	func _on_join_requested(lobby_id: int, friend_id: int) -> void:
		var backend: SteamBackend = _backend_ref.get_ref()
		if not backend:
			return
		
		var active_tree: MultiplayerTree = SteamBackend._active.get_ref()
		if not active_tree:
			return
		
		active_tree.invite_received.emit(str(lobby_id), friend_id)
		
		if backend.auto_join_on_invite:
			var username := _wrapper.get_persona_name()
			active_tree.join(str(lobby_id), username)
