## Abstract base for server directory integrations (Steam, social services, etc.).
##
## A [LobbyDirectory] runs as a service registered via [NetwServices] and manages
## the lifecycle of remote lobbies: listing, hosting, and joining. Instead of
## adopting peers internally, it produces a [MultiplayerPeer] on demand for the
## matched [BackendPeer] to assign to the multiplayer tree.
##
## [br][br]
## A directory dropped under the [MultiplayerTree] is discovered automatically.
## [method NetwServices.register] under its own concrete script is enough.
## [ConnectSession] collects every [LobbyDirectory] through
## [method MultiplayerTree.get_services], so there is no per directory wiring in
## the browser. Implementations must:
## [br]- Register via [code]NetwServices.register(self)[/code] on
##   [code]_enter_tree[/code].
## [br]- Implement [method host_lobby] and [method join_lobby_peer] to return
##   a live, connected [MultiplayerPeer] (or [code]null[/code] on failure).
@abstract
class_name LobbyDirectory
extends Node

## Emitted after a browse request resolves. UIs should replace their list
## with [param lobbies] (clear-then-fill).
signal lobby_list_updated(lobbies: Array[LobbyInfo])

## Emitted when an external invite is received (e.g. Steam overlay).
signal invite_received(lobby_id: int, sender_id: int)

## Emitted when the directory determines that the transport is unavailable.
signal provider_unavailable(reason: String)


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
func make_join_target(lobby: LobbyInfo) -> JoinTarget


## Creates a new lobby and builds a hosting [MultiplayerPeer].
##
## Must return a live, connected peer, or [code]null[/code] on failure.
@abstract
func host_lobby(server_name: String) -> MultiplayerPeer


## Joins an existing lobby and builds a connecting [MultiplayerPeer].
##
## Must return a live, connected peer, or [code]null[/code] on failure.
@abstract
func join_lobby_peer(lobby_id: int) -> MultiplayerPeer
