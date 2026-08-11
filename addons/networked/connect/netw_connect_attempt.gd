## One host or join call, from preparation to a terminal [NetwConnectResult].
##
## A [NetwConnectAttempt] owns the transient state of establishing a connection
## and nothing after it. It moves through [enum State] and resolves
## [signal finished] exactly once. Per-attempt state dies with the attempt, so
## there is no shared object to scrub between connections.
## [codeblock]
## PREPARING    ─ await api.prepare_join(payload)
## CONSTRUCTING ─ peer = await transport._join(self, target)
## CONNECTING   ─ api.multiplayer_peer = peer   (the assignment edge)
## DONE         ─ finished.emit(result)
## [/codeblock]
class_name NetwConnectAttempt
extends RefCounted

## Stage of an attempt's lifecycle.
enum State {
	## Awaiting [method NetwMultiplayer.prepare_join] credential preparation.
	PREPARING,
	## Awaiting the transport's peer construction.
	CONSTRUCTING,
	## The peer is assigned and the attempt observes the session machine.
	CONNECTING,
	## The attempt has resolved [signal finished].
	DONE,
}

## Emitted as the attempt advances, with an eased [param ratio] in
## [code][0, 1][/code].
signal progress(step: StringName, message: String, ratio: float)

## Emitted once with the terminal [param result].
signal finished(result: NetwConnectResult)

## The current [enum State].
var state: State = State.PREPARING

## The terminal outcome, or [code]null[/code] until [signal finished].
var result: NetwConnectResult

## The [NetwPeerView] bound to the assigned peer, or [code]null[/code] before
## [constant State.CONNECTING].
var view: NetwPeerView

## The [NetwConnectTarget] for a join attempt, or [code]null[/code] for a host.
var target: NetwConnectTarget

## The session this attempt connects, taken at construction.
##
## A transport reads it during construction to resolve session-scoped services
## and facts (a lobby directory, the [member NetwSessionHandle.app_id]).
var api: NetwMultiplayer:
	get:
		return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null

## Transport-private scratch carried from construction to
## [method NetwTransport._make_view].
##
## A transport that builds live state during [method NetwTransport._join] or
## [method NetwTransport._host] (a WebRTC session and signaler) stashes it here so
## its view can adopt it, since the transport itself stays stateless.
var context: Dictionary = { }

# The owning session. Weakly held because the api outlives the attempt.
var _api_ref: WeakRef

# Guards a single terminal resolution across abort and completion races.
var _finished := false


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null


## Returns [code]true[/code] once the attempt has resolved.
func is_done() -> bool:
	return state == State.DONE


## Reports intermediate progress to observers.
##
## Called by [NetwConnector] at each stage and by a [NetwTransport] during
## construction. Ignored after the attempt resolves.
func report(step: StringName, message: String, ratio: float) -> void:
	if _finished:
		return
	progress.emit(step, message, ratio)


## Resolves the attempt with [param outcome], if it has not resolved already.
##
## [NetwConnector] calls this to drive the terminal outcome. [method abort] is
## the public cancel entry point.
func resolve(outcome: NetwConnectResult) -> void:
	_finish(outcome)


## Aborts the attempt at any stage.
##
## The peer a [constant State.CONNECTING] abort leaves behind is unwound by
## [method NetwConnector.abort], which assigns an [OfflineMultiplayerPeer] as
## the session machine's cancel edge.
func abort() -> void:
	_finish(NetwConnectResult.aborted())


# Resolves the attempt exactly once and emits the terminal result.
func _finish(outcome: NetwConnectResult) -> void:
	if _finished:
		return
	_finished = true
	result = outcome
	state = State.DONE
	finished.emit(outcome)
