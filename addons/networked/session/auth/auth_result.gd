## The outcome of a [method NetwAuthFlow.verify] call: an accepted
## [NetwIdentity] or a rejection reason.
##
## Building the result through [method accept] or [method reject] replaces the
## old mutable rejection field, so a server verifying several peers concurrently
## never shares reason state between them.
## [codeblock]
## func verify(peer_id: int, data: PackedByteArray) -> AuthResult:
##     if not _valid(data):
##         return AuthResult.reject("bad ticket")
##     return AuthResult.accept(NetwIdentity.from_ticket(data))
## [/codeblock]
class_name AuthResult
extends RefCounted

## Whether the peer was accepted.
var accepted: bool = false

## The verified identity when [member accepted], otherwise [code]null[/code].
var identity: NetwIdentity = null

## The human-readable reason when rejected, otherwise empty.
var rejection_reason: String = ""


## Builds an accepting result carrying [param identity].
static func accept(identity: NetwIdentity) -> AuthResult:
	var result := AuthResult.new()
	result.accepted = true
	result.identity = identity
	return result


## Builds a rejecting result carrying [param reason].
static func reject(reason: String) -> AuthResult:
	var result := AuthResult.new()
	result.accepted = false
	result.rejection_reason = reason
	return result
