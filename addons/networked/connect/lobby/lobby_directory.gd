## Abstract base for server directory integrations (Steam, social services, etc.).
##
## A [LobbyDirectory] runs as a service registered via [NetwServices] and manages
## the lifecycle of remote lobbies: listing, hosting, and joining. Instead of
## adopting peers internally, it produces a [MultiplayerPeer] on demand for the
## matched [BackendPeer] to assign to the multiplayer tree.
##
## [br][br]
## A directory dropped under the [MultiplayerTree] is discovered automatically.
## [LobbyDirectory] extends [NetwService], so a subclass registers itself under
## its own concrete script with no boilerplate. [ConnectSession] collects every
## [LobbyDirectory] through [method MultiplayerTree.get_services], so there is no
## per directory wiring in the browser. Implementations must:
## [br]- Implement [method host_lobby] and [method join_lobby_peer] to return
##   a live, connected [MultiplayerPeer] (or [code]null[/code] on failure).
## [br]- Declare what they can honor through [method capabilities] so a browser
##   hides controls a transport cannot back, instead of letting them no-op.
@abstract
class_name LobbyDirectory
extends NetwService

## How widely a hosted lobby is advertised, passed in [LobbyDirectory.HostOptions].
##
## Every transport agrees on these three tiers, but degrades the ones it cannot
## back. A directory that lacks [constant Capability.FRIENDS_ONLY] treats
## [constant Visibility.FRIENDS_ONLY] as [constant Visibility.PRIVATE] and logs
## the downgrade rather than leaking the lobby publicly.
## [codeblock]
## PUBLIC        discoverable through list_lobbies, joinable by anyone
## FRIENDS_ONLY  hidden from browse, only friends see or join (needs identity)
## PRIVATE       unlisted, reachable only by sharing its id out of band
## [/codeblock]
enum Visibility {
	PUBLIC,
	FRIENDS_ONLY,
	PRIVATE,
}

## Bit flags a directory ORs together in [method capabilities] to advertise the
## lobby features its transport can honor.
##
## A browser tests these with [method supports] before exposing a control, so a
## friends-only toggle never appears for a WebTorrent board that has no identity
## graph. [constant Capability.BROWSE] means [method list_lobbies] returns real
## results, [constant Capability.FRIENDS_ONLY] backs that [Visibility] tier,
## [constant Capability.INVITES] backs out-of-band invites, and
## [constant Capability.FRIEND_NAMES] backs persona resolution in
## [method get_member_name].
enum Capability {
	BROWSE = 1,
	FRIENDS_ONLY = 2,
	INVITES = 4,
	FRIEND_NAMES = 8,
}


## Plain data describing a single discoverable lobby.
##
## [signal lobby_list_updated] emits batches of [LobbyDirectory.LobbyInfo]
## values. UIs render one entry per [LobbyDirectory.LobbyInfo] without caring
## about the underlying transport.
class LobbyInfo:
	extends Resource

	## Transport-specific lobby identifier.
	@export var id: int = 0

	## Human-readable lobby name as advertised by the host.
	@export var lobby_name: String = ""

	## Display name of the host that owns the lobby, when the transport resolves
	## it. Empty when the host identity is unknown.
	@export var host_name: String = ""

	## Current member count, including the host.
	@export var players: int = 0

	## Maximum member count the host configured.
	@export var max_players: int = 0

	## Advertised [enum Visibility] of the lobby. A browse list only ever carries
	## [constant Visibility.PUBLIC] and [constant Visibility.FRIENDS_ONLY]
	## entries, so this distinguishes the two for the UI.
	@export var visibility: Visibility = Visibility.PUBLIC

	## When [code]false[/code], the host has locked the lobby and a join attempt
	## is expected to be rejected.
	@export var joinable: bool = true

	## Free-form provider-specific metadata.
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


## Inputs to [method host_lobby], so the host call carries visibility and
## capacity instead of a bare name.
##
## A directory reads only the fields its transport can honor and falls back to
## its own export defaults for the rest. [member max_players] of [code]0[/code]
## means "use the directory default", and [member visibility] is mapped or
## downgraded per [enum Visibility].
## [codeblock]
## var opts := LobbyDirectory.HostOptions.make("My Game", LobbyDirectory.Visibility.FRIENDS_ONLY)
## var peer := await directory.host_lobby(opts)
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

## Emitted after a browse request resolves. UIs should replace their list
## with [param lobbies] (clear-then-fill).
signal lobby_list_updated(lobbies: Array[LobbyDirectory.LobbyInfo])

## Emitted when an external invite is received (e.g. Steam overlay).
signal invite_received(lobby_id: int, sender_id: int)

## Emitted when the directory determines that the transport is unavailable.
signal provider_unavailable(reason: String)


## Returns the [enum Capability] flags this directory can honor, ORed together.
##
## The base directory advertises nothing, so a subclass that omits an override
## reads as a non-browsable, no-frills transport. A browser calls
## [method supports] against this to gate its controls.
func capabilities() -> int:
	return 0


## Returns [code]true[/code] when [method capabilities] includes [param cap].
func supports(cap: Capability) -> bool:
	return (capabilities() & cap) != 0


## Requests an updated lobby list. Result delivered asynchronously via
## [signal lobby_list_updated].
@abstract
func list_lobbies() -> void


## Leaves the current lobby, if any. Idempotent.
@abstract
func leave_lobby() -> void


## Resolves a Godot multiplayer [param peer_id] to a human-readable display
## name. Default returns [code]"Player %d"[/code], or transport-specific
## providers (Steam, Discord, ...) should override to resolve personas.
func get_member_name(peer_id: int) -> String:
	return "Player %d" % peer_id


## Returns the local lobby member's display name.
##
## Directories may override this when the local identity is known before a
## Godot peer ID can be resolved.
func get_local_member_name() -> String:
	return "Player"


## Returns a configured [JoinTarget] pointing at the given [param lobby],
## stamped with the directory's own [BackendPeer] template.
@abstract
func make_join_target(lobby: LobbyDirectory.LobbyInfo) -> JoinTarget


## Creates a new lobby per [param options] and builds a hosting
## [MultiplayerPeer].
##
## The directory maps [member HostOptions.visibility] to its transport, or
## downgrades it to [constant Visibility.PRIVATE] when it lacks
## [constant Capability.FRIENDS_ONLY]. Must return a live, connected peer, or
## [code]null[/code] on failure.
@abstract
func host_lobby(options: LobbyDirectory.HostOptions) -> MultiplayerPeer


## Joins an existing lobby and builds a connecting [MultiplayerPeer].
##
## Must return a live, connected peer, or [code]null[/code] on failure.
@abstract
func join_lobby_peer(lobby_id: int) -> MultiplayerPeer
