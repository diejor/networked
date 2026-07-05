## Service contract for lobby discovery providers.
##
## A [LobbyDirectory] lists, hosts, and joins provider lobbies. It never installs
## peers on the tree. [method host_lobby] and [method join_lobby_peer] return a
## connected [MultiplayerPeer] for the caller to adopt.
##
## [br][br]
## [ConnectSession] collects every [LobbyDirectory] service through
## [method MultiplayerTree.get_services]. [method capabilities] tells a browser
## which controls the provider can honor.
## [codeblock]
## MultiplayerTree
## └── LobbyDirectory
##     ├── list_lobbies() -> lobby_list_updated
##     ├── host_lobby(options) -> MultiplayerPeer
##     └── join_lobby_peer(id) -> MultiplayerPeer
## [/codeblock]
@abstract
class_name LobbyDirectory
extends NetwService

## How widely a hosted lobby is advertised.
##
## A directory that lacks [constant Capability.FRIENDS_ONLY_SUPPORT] treats
## [constant Visibility.FRIENDS_ONLY] as [constant Visibility.PRIVATE].
## [codeblock]
## PUBLIC        browse-visible.
## FRIENDS_ONLY  identity-gated.
## PRIVATE       join-by-id only.
## [/codeblock]
enum Visibility {
	PUBLIC,
	FRIENDS_ONLY,
	PRIVATE,
}

## Bit flags returned by [method capabilities].
##
## Browsers use [method supports] to hide controls the provider cannot honor.
## [codeblock]
## Capability
## ├── BROWSE        # list_lobbies returns provider results.
## ├── FRIENDS_ONLY_SUPPORT  # Visibility.FRIENDS_ONLY is supported.
## ├── INVITES               # invite_received can fire.
## └── FRIEND_NAMES          # get_member_name resolves personas.
## [/codeblock]
enum Capability {
	BROWSE = 1,
	FRIENDS_ONLY_SUPPORT = 2,
	INVITES = 4,
	FRIEND_NAMES = 8,
}


## Plain data for one discoverable lobby.
##
## [signal lobby_list_updated] emits arrays of [LobbyDirectory.LobbyInfo].
## [method make_join_target] turns one entry into a [JoinTarget].
## [codeblock]
## LobbyInfo
## ├── id
## ├── lobby_name
## ├── players / max_players
## ├── visibility
## ├── joinable
## └── metadata
## [/codeblock]
class LobbyInfo:
	extends Resource

	## Provider-specific lobby identifier.
	@export var id: int = 0

	## Human-readable lobby name as advertised by the host.
	@export var lobby_name: String = ""

	## Host display name. Empty when the provider cannot resolve it.
	@export var host_name: String = ""

	## Current member count, including the host.
	@export var players: int = 0

	## Maximum member count the host configured.
	@export var max_players: int = 0

	## Advertised [enum Visibility] of the lobby.
	@export var visibility: Visibility = Visibility.PUBLIC

	## When [code]false[/code], joining is expected to fail.
	@export var joinable: bool = true

	## Provider-specific metadata copied into [JoinTarget].
	@export var metadata: Dictionary = { }


	## Creates a [LobbyDirectory.LobbyInfo] from provider data.
	static func make(
			id: int,
			lobby_name: String,
			players: int,
			max_players: int,
			metadata: Dictionary = { },
			host_name: String = "",
			visibility: Visibility = Visibility.PUBLIC,
			joinable: bool = true,
	) -> LobbyInfo:
		var info := LobbyInfo.new()
		info.id = id
		info.lobby_name = lobby_name
		info.players = players
		info.max_players = max_players
		info.metadata = metadata
		info.host_name = host_name
		info.visibility = visibility
		info.joinable = joinable
		return info


## Inputs to [method host_lobby].
##
## A directory reads only the fields its provider can honor. [member max_players]
## of [code]0[/code] means "use the directory default".
## [codeblock]
## HostOptions
## ├── server_name
## ├── visibility
## └── max_players
## [/codeblock]
class HostOptions:
	extends Resource

	## User-facing lobby name advertised to browsers.
	@export var server_name: String = ""

	## Requested [enum Visibility], mapped or downgraded by the directory.
	@export var visibility: Visibility = Visibility.PUBLIC

	## Maximum member count. [code]0[/code] keeps the directory's own default.
	@export var max_players: int = 0


	## Creates a [LobbyDirectory.HostOptions] for a host call.
	static func make(
			server_name: String,
			visibility: Visibility = Visibility.PUBLIC,
			max_players: int = 0,
	) -> HostOptions:
		var opts := HostOptions.new()
		opts.server_name = server_name
		opts.visibility = visibility
		opts.max_players = max_players
		return opts

## Emitted after [method list_lobbies] resolves.
##
## UIs should replace their current rows with [param lobbies].
signal lobby_list_updated(lobbies: Array[LobbyDirectory.LobbyInfo])

## Emitted when an external invite is received (e.g. Steam overlay).
signal invite_received(lobby_id: int, sender_id: int)

## Emitted when the directory determines that the transport is unavailable.
signal provider_unavailable(reason: String)


## Returns the [enum Capability] flags this directory can honor.
##
## The base directory advertises nothing. A browser calls [method supports] to
## gate controls.
func capabilities() -> int:
	return 0


## Returns [code]true[/code] when [method capabilities] includes [param cap].
func supports(cap: Capability) -> bool:
	return (capabilities() & cap) != 0


## Requests an updated lobby list.
##
## Results arrive through [signal lobby_list_updated].
@abstract
func list_lobbies() -> void


## Leaves the current lobby, if any. Idempotent.
@abstract
func leave_lobby() -> void


## Resolves [param peer_id] to a display name.
##
## Providers with identity graphs should override this.
func get_member_name(peer_id: int) -> String:
	return "Player %d" % peer_id


## Returns the local lobby member display name.
##
## Providers may override this before a Godot peer id exists.
func get_local_member_name() -> String:
	return "Player"


## Returns a [JoinTarget] for [param lobby].
##
## The target should carry the provider address and matching [BackendPeer].
@abstract
func make_join_target(lobby: LobbyDirectory.LobbyInfo) -> JoinTarget


## Creates a lobby and returns a connected host [MultiplayerPeer].
##
## Returns [code]null[/code] on failure.
@abstract
func host_lobby(options: LobbyDirectory.HostOptions) -> MultiplayerPeer


## Joins an existing lobby and returns a connected [MultiplayerPeer].
##
## Returns [code]null[/code] on failure.
@abstract
func join_lobby_peer(lobby_id: int) -> MultiplayerPeer
