## [BackendPeer] that hosts and joins through a Nakama relay match.
##
## Delegates every transport operation to the registered [NakamaLobbyDirectory],
## which owns the authenticated socket and the bridge-driven [MultiplayerPeer].
## Because the relay never needs a listening socket, this backend reports
## [method can_host] as [code]true[/code] even on web exports, unlike
## [WebSocketBackend].
## [codeblock]
## var target := JoinTarget.new()
## target.backend = NakamaBackend.new()
## target.address = match_id   # opaque Nakama match id
## [/codeblock]
@tool
class_name NakamaBackend
extends BackendPeer

var _dir: NakamaLobbyDirectory


## Resolves the [NakamaLobbyDirectory] service.
func setup(tree: MultiplayerTree) -> Error:
	_dir = tree.get_service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	if _dir == null:
		_dir = tree.get_service(LobbyDirectory) as NakamaLobbyDirectory
	if _dir == null:
		Netw.dbg.warn(
			"NakamaBackend: NakamaLobbyDirectory service not registered.",
		)
		return ERR_UNCONFIGURED
	return OK


## Implements [method BackendPeer.create_host_peer] by creating a relay match.
func create_host_peer(
		_tree: MultiplayerTree,
		options: LobbyDirectory.HostOptions = null,
) -> MultiplayerPeer:
	Netw.dbg.trace("NakamaBackend: create_host_peer called.")
	if _dir == null:
		return null
	var opts := options if options != null else LobbyDirectory.HostOptions.new()
	return await _dir.host_lobby(opts)


## Implements [method BackendPeer.create_join_peer] with a relay match id.
func create_join_peer(
		_tree: MultiplayerTree,
		server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	Netw.dbg.trace(
		"NakamaBackend: create_join_peer called at match %s.",
		[server_address],
	)
	if _dir == null:
		return null
	return await _dir.join_match_peer(server_address)


## Returns the active relay match id for [method MultiplayerTree.join].
func get_join_address() -> String:
	return _dir.get_join_address() if _dir != null else ""


## Nakama uses relay matchmaking instead of embedded local hosting.
func supports_embedded_server() -> bool:
	return false


## Implements [method BackendPeer.is_available]. Requires the Nakama addon.
func is_available() -> bool:
	return NakamaWrapper.is_addon_present()


## Implements [method BackendPeer.can_host]. The relay needs no listening
## socket, so a web export can host.
func can_host() -> bool:
	return NakamaWrapper.is_addon_present()


## Relay match ids are not browse-probable, so this stays unsupported.
func probe_server_info(
		_address: String,
		_timeout: float = 2.0,
) -> BackendPeer.ProbeResult:
	return BackendPeer.ProbeResult.unsupported()


## Returns a [code]"Match ID"[/code] [BackendPeer.AddressHint].
func get_address_hint() -> BackendPeer.AddressHint:
	return BackendPeer.AddressHint.make(
		"Match ID",
		"",
		"Paste the Nakama match id shared by the host.",
		false,
		false,
	)


## Preserves authored settings after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	pass


## Returns the display name for this backend.
func get_display_name() -> String:
	return "Nakama"
