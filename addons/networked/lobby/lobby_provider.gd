## Abstract base for lobby providers (Steam, Discord, custom matchmaking, ...).
##
## A [LobbyProvider] runs as a [b]descendant of [MultiplayerTree][/b] and owns
## the lifecycle of an external lobby system: creating, browsing, joining,
## leaving lobbies, and producing a connected [MultiplayerPeer]. After a peer
## is ready, callers (typically a lobby UI) hand it to
## [method NetwTree.adopt_peer] - or use [method bind] to do that plus wire
## the standard tree-to-lobby sync.
## [br][br]
## Implementations should:
## [br]- Register with [code]NetwServices.register(self, LobbyProvider)[/code]
##   on [code]_enter_tree[/code].
## [br]- Emit [signal peer_ready] once a connected peer exists.
## [br]- Emit [signal lobby_list_updated] with clear-then-fill semantics.
## [br]- Treat [method create_lobby] and [method join_lobby] as fire-and-forget;
##   completion is reported via signals.
@abstract
class_name LobbyProvider
extends Node

## Emitted once after a lobby has been created and a connected peer exists.
signal lobby_created(lobby_id: int)

## Emitted once after the local client successfully joins a lobby.
signal lobby_joined(lobby_id: int)

## Emitted when a create or join attempt fails. [param reason] is a
## human-readable string suitable for UI display.
signal lobby_join_failed(reason: String)

## Emitted after a browse request resolves. UIs should replace their list
## with [param lobbies] (clear-then-fill).
signal lobby_list_updated(lobbies: Array[LobbyInfo])

## Emitted when an external invite is received (e.g. Steam overlay).
signal invite_received(lobby_id: int, sender_id: int)

## Emitted once a [MultiplayerPeer] is connected and ready to be adopted.
signal peer_ready(peer: MultiplayerPeer)

## Emitted when the provider determined the transport is unavailable.
signal provider_unavailable(reason: String)


## Creates a new lobby with display name [param lobby_name]. Fire-and-forget.
## Completion is reported via [signal lobby_created] / [signal lobby_join_failed].
@abstract
func create_lobby(lobby_name: String) -> void


## Joins an existing lobby by transport-specific [param lobby_id].
## Completion is reported via [signal lobby_joined] / [signal lobby_join_failed].
@abstract
func join_lobby(lobby_id: int) -> void


## Requests an updated lobby list. Result delivered via
## [signal lobby_list_updated].
@abstract
func list_lobbies() -> void


## Leaves the current lobby, if any. Idempotent.
@abstract
func leave_lobby() -> void


## Returns the locally owned [MultiplayerPeer] produced by the active
## lobby, or [code]null[/code] when no lobby is active.
@abstract
func get_peer() -> MultiplayerPeer


## Resolves a Godot multiplayer [param peer_id] to a human-readable display
## name. Default returns [code]"Player %d"[/code]; transport-specific
## providers (Steam, Discord, ...) should override to resolve personas.
func get_member_name(peer_id: int) -> String:
	return "Player %d" % peer_id


## Adopts the produced peer onto [param tree] and wires standard lifecycle
## sync (advertised member count, leave-on-disconnect).
##
## If [param join_payload] is provided, it is passed to
## [method NetwTree.adopt_peer] to trigger the session join.
##
## Call once per lobby session after [signal peer_ready] (or after
## [signal lobby_created] / [signal lobby_joined], which imply it).
func bind(tree: NetwTree, join_payload: JoinPayload = null) -> Error:
	var peer := get_peer()
	if peer == null:
		return ERR_UNCONFIGURED
	var err := tree.adopt_peer(peer, join_payload)
	if err != OK:
		return err
	_bind_tree_signals(tree)
	return OK


## Wires provider-specific reactions to tree lifecycle. Override to advertise
## member counts, react to disconnects, etc. Base implementation hooks
## [signal NetwTree.server_disconnecting] to call [method leave_lobby].
func _bind_tree_signals(tree: NetwTree) -> void:
	if not tree.server_disconnecting.is_connected(_on_tree_server_disconnecting):
		tree.server_disconnecting.connect(_on_tree_server_disconnecting)


func _on_tree_server_disconnecting(_reason: String) -> void:
	leave_lobby()
