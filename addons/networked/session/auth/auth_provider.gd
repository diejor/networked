## Base resource for [MultiplayerTree] peer authentication.
##
## [MultiplayerTree] calls [method prepare], [method get_credentials],
## [method authenticate], and [method get_host_identity] around Godot's auth
## phase so a provider can turn transport-specific proof into a
## [NetwIdentity].
## [codeblock]
## Client:
##     prepare(payload) -> OK
##     get_credentials(payload) -> PackedByteArray
##
## Server:
##     authenticate(peer_id, data) -> NetwIdentity or null
##
## Listen server:
##     get_host_identity() -> NetwIdentity or null
## [/codeblock]
class_name NetwAuth
extends Resource

## Human-readable reason for the last authentication rejection.
##
## [method authenticate] sets this member before returning [code]null[/code]
## so [AuthCoordinator] can pass a useful rejection message through
## [SessionRoster].
var rejection_reason: String


## Prepares [param payload] before the transport opens.
##
## The owning [MultiplayerTree] entry call awaits this method before
## [method get_credentials] can be used during Godot's auth phase.
func prepare(payload: JoinPayload) -> Error:
	return OK


## Builds proof bytes for [param payload].
##
## [AuthCoordinator] sends the returned bytes with
## [method SceneMultiplayer.send_auth]. Return non-empty bytes when
## [member MultiplayerTree.auth_provider] is active.
func get_credentials(payload: JoinPayload) -> PackedByteArray:
	return PackedByteArray()


## Validates [param data] for [param peer_id].
##
## Return a [NetwIdentity] to accept the peer, or [code]null[/code] to reject.
## Set [member rejection_reason] before rejecting.
##
## [br][br][b]Server Only.[/b]
func authenticate(peer_id: int, data: PackedByteArray) -> NetwIdentity:
	return null


## Builds the listen-server host identity.
##
## Godot does not run [member SceneMultiplayer.auth_callback] for peer
## [constant MultiplayerPeer.TARGET_PEER_SERVER], so [AuthCoordinator] calls
## this method after [method MultiplayerTree.host].
func get_host_identity() -> NetwIdentity:
	return null
