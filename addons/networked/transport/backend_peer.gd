## Abstract base resource for network transports used by [MultiplayerTree].
##
## Subclass this to implement a new transport (ENet, WebSocket, WebRTC, etc.).
## Override [method create_host_peer] and [method create_join_peer] to produce
## a [MultiplayerPeer]; the [MultiplayerTree] owns the [SceneMultiplayer] and
## assigns the returned peer onto it.
@tool
@abstract
class_name BackendPeer
extends Resource


## Optional one-time setup hook called by [MultiplayerTree] before
## [method create_host_peer] or [method create_join_peer]. Use it to resolve
## scene-relative nodes or external services. Return [code]OK[/code] on success.
func setup(_tree: MultiplayerTree) -> Error:
	return OK


## Produces a [MultiplayerPeer] in server mode. May [code]await[/code].
##
## Return [code]null[/code] to signal failure; the tree will treat this as
## [code]ERR_CANT_CREATE[/code]. The tree assigns the returned peer onto its
## owned [SceneMultiplayer].
@abstract
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer


## Produces a [MultiplayerPeer] in client mode connecting to [param address].
## May [code]await[/code]. Return [code]null[/code] to signal failure.
@abstract
func create_join_peer(
	_tree: MultiplayerTree, _address: String, _username: String = ""
) -> MultiplayerPeer


## Returns editor configuration warnings specific to this backend for the given [param tree].
@abstract
func _get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray


## Per-frame poll hook for backends that drive their own internal state
## (e.g. WebRTC signaling sockets, in-process loopback queues).
##
## The tree polls the owned [SceneMultiplayer] separately - do not poll the
## api here.
func poll(_dt: float) -> void:
	pass


## Closes and clears any backend-side state. Called by [MultiplayerTree] before
## opening a new session and on teardown. Override to release transport-specific
## handles (e.g. tracker sockets, lobby memberships).
func peer_reset_state() -> void:
	pass


## Returns the address clients should use to join a hosted session.
##
## Override in subclasses that use dynamic addresses (e.g. room codes). Defaults to [code]"localhost"[/code].
func get_join_address() -> String:
	return "localhost"


## Returns [code]true[/code] if this backend supports spinning up an embedded
## server on a local machine (e.g. ENet, WebSocket).
##
## Return [code]false[/code] for backends that rely on external lobby systems
## (e.g. Steam).
func supports_embedded_server() -> bool:
	return true


## Returns [code]true[/code] if [method MultiplayerTree.connect_player] can
## probe an existing local session through [code]"localhost"[/code].
##
## Backends with session-id or lobby based joins should return
## [code]false[/code] even when [method supports_embedded_server] is
## [code]true[/code].
func supports_local_probe() -> bool:
	return supports_embedded_server()


## Called after this backend is duplicated by [MultiplayerTree]'s backend setter.
##
## Override to preserve shared references that [method Resource.duplicate] would reset
## to their default values.
func _copy_from(_source: BackendPeer) -> void:
	pass
