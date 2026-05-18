## Abstract base class for authentication providers.
##
## Three lifecycle hooks called by [MultiplayerTree]:
## [br][br]
## [b]Client side:[/b]
## [br]1. [method _prepare] -- before transport opens. Populates
##     [JoinPayload] with metadata. Returns [constant OK] to proceed,
##     or an [enum Error] to abort [method MultiplayerTree.connect_player].
## [br]2. [method _get_credentials] -- during Godot's auth phase.
##     Returns proof bytes sent via [method SceneMultiplayer.send_auth].
##
## [br][br]
## [b]Server side:[/b]
## [br]3. [method _authenticate] -- when
##     [member SceneMultiplayer.auth_callback] fires. Validates proof
##     bytes. Returns [NetwIdentity] to accept, [code]null[/code] to
##     reject. Set [member rejection_reason] on failure so the server can
##     communicate why.
##
## [br][br]
## [b]Listen-server host:[/b]
## [br]4. [method _get_host_identity] -- after [method MultiplayerTree.host] for
##     peer 1. Godot's auth callback never fires for the local host, so this
##     provides a separate path. Returns [code]null[/code] to skip host auth.
class_name NetwAuthProvider
extends Resource

## Human-readable reason for the last authentication rejection.
## Set by [method _authenticate] on failure so the server can
## communicate why a peer was disconnected.
var rejection_reason: String


## Client-side. Called before the transport opens.
##
## Populates [param payload] with provider metadata. Return [constant OK]
## to proceed, or an [enum Error] to abort the connection.
##
## [b]Note:[/b] Implementations may [code]await[/code] HTTP calls or
## browser flows. [MultiplayerTree.connect_player] awaits this method.
func _prepare(payload: JoinPayload) -> Error:
	return OK


## Client-side. Called when [signal SceneMultiplayer.peer_authenticating]
## fires for the server peer.
##
## Returns cryptographic proof bytes (ticket, token, JWT) to be sent
## via [method SceneMultiplayer.send_auth]. Return non-empty bytes when
## [member MultiplayerTree.auth_provider] is active.
func _get_credentials(payload: JoinPayload) -> PackedByteArray:
	return PackedByteArray()


## Server-side. Called when [member SceneMultiplayer.auth_callback] fires.
##
## Validates [param data] for [param peer_id]. Return a [NetwIdentity]
## to accept the peer, or [code]null[/code] to reject. Set
## [member rejection_reason] on failure.
func _authenticate(peer_id: int, data: PackedByteArray) -> NetwIdentity:
	return null


## Listen-server host. Called after [method MultiplayerTree.host] for peer 1.
##
## Returns a [NetwIdentity] for the host peer, or [code]null[/code] to
## skip host authentication. Default implementations of real providers
## (Steam, etc.) override this.
func _get_host_identity() -> NetwIdentity:
	return null
