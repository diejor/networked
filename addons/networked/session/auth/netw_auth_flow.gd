## Per-session authentication flow for [NetwMultiplayer].
##
## Registered as a factory through [method Netw.configure_auth] so every session
## (including a debugger-cloned tree) constructs its own flow and owns its
## per-attempt state, such as a fetched ticket or nonce, for exactly one session's
## attempts. The four hooks live on one class so a missing hook fails at
## registration, not when a listen-server host cannot join its own session.
## [codeblock]
## Netw.configure_auth(func(api: NetwMultiplayer) -> NetwAuthFlow:
##     return SteamAuthFlow.new(api))
##
## class SteamAuthFlow extends NetwAuthFlow:
##     var _ticket: PackedByteArray
##     func prepare(_payload: JoinPayload) -> Error:
##         _ticket = await SteamAuth.fetch_ticket()
##         return OK if _ticket.size() > 0 else ERR_UNAUTHORIZED
##     func credentials(_payload: JoinPayload) -> PackedByteArray:
##         return _ticket
##     func verify(peer_id: int, data: PackedByteArray) -> AuthResult:
##         return AuthResult.accept(NetwIdentity.from_ticket(data))
## [/codeblock]
class_name NetwAuthFlow
extends RefCounted

## Prepares the flow before the transport opens, awaited by
## [method NetwSessionHandle.prepare_join]. Fetch a ticket here and return a
## non-[constant OK] [enum Error] to abort the join.
func prepare(_payload: JoinPayload) -> Error:
	return OK


## Returns the proof bytes sent with the client hello. Return non-empty bytes
## when the server verifies credentials.
func credentials(_payload: JoinPayload) -> PackedByteArray:
	return PackedByteArray()


## Validates [param data] for [param peer_id], returning
## [method AuthResult.accept] with an identity or [method AuthResult.reject] with
## a reason.
##
## [br][br][b]Server Only.[/b]
func verify(_peer_id: int, _data: PackedByteArray) -> AuthResult:
	return AuthResult.reject("Authentication flow did not implement verify")


## Returns the listen-server host's own identity. Godot never runs the auth
## callback for the host peer, so [AuthCoordinator] calls this when a configured
## server reaches [constant NetwMultiplayer.SessionState.ONLINE].
func host_identity() -> NetwIdentity:
	return null
