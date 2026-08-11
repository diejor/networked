## What a session admits, suspends, and tears down, reached through
## [member NetwMultiplayer.session].
##
## [NetwMultiplayer] outlives any one session: it goes offline, hosts, leaves,
## and hosts again on the same object. This handle is that sub-noun, so joining,
## tearing a session down, suspending it, and setting the policies that admit a
## peer all read as one group instead of scattering across the API. What the
## session currently [i]is[/i] stays on the API, because
## [member NetwMultiplayer.role] and [member NetwMultiplayer.state] have derived
## readers there. Opening a connection is not here at all: a peer arrives from
## [NetwConnector], and this machine answers for what the session becomes.
## [codeblock]
## var session := NetwMultiplayer.of(self).session
##
## session.set_auth_flow(MyAuth.new())      # policy, before bring-up
## await NetwConnector.of(api).host(payload)
## ...
## await session.leave()
## [/codeblock]
class_name NetwSessionHandle
extends RefCounted

## The [NetwSessionConfig] this session was registered with.
##
## Never [code]null[/code]: a session with no embedding reads a default config,
## so [member NetwSessionConfig.app_id] and
## [member NetwSessionConfig.link_conditions] answer before any
## [MultiplayerTree] registers rather than making every reader null-check.
var config: NetwSessionConfig:
	get:
		return _core.config

## The game-build tag admission gates on, from
## [member NetwSessionConfig.app_id]. Empty disables the gate.
var app_id: StringName:
	get:
		return _core.app_id

## The player cap this session advertises, zero while it is not hosting.
##
## Whatever opened the host publishes the cap it resolved, and a probe reply
## reads it back through [method NetwServerInfo.from_session], so the advertised
## number is a session fact rather than a re-reading of the host configuration.
var advertised_max_players: int:
	get:
		return _core.advertised_max_players
	set(value):
		_core.advertised_max_players = value

var _core: SessionCore


func _init(core: SessionCore) -> void:
	_core = core

#region ── Bring-up ────────────────────────────────────────────────────────────

## Validates [param join_payload] and prepares the credentials the next
## connection authenticates with, without assigning a transport peer.
##
## Bring-up prepares for its caller, so this is the verb a flow that opens its
## own peer calls before assigning it. A client submits the prepared payload on
## reaching [constant SessionCore.State.ONLINE]; a host holds it until an
## explicit [method submit_join]. See [method SessionCore.prepare_join].
##
## [br][br][b]Player request.[/b]
func prepare_join(join_payload: JoinPayload) -> Error:
	return await _core.prepare_join(join_payload)


## Submits [param join_payload] to server authority over the session's join
## channel.
##
## A client submits its prepared payload automatically on reaching
## [constant SessionCore.State.ONLINE], so this is the verb a rejoin or a custom
## connect flow uses when the peer is already online and only the identity is
## being (re)sent. See [method SessionCore.submit_join].
##
## [br][br][b]Player request.[/b]
func submit_join(join_payload: JoinPayload) -> void:
	_core.submit_join(join_payload)

#endregion

#region ── Teardown ────────────────────────────────────────────────────────────

## Saves game state, closes the multiplayer peer, and waits for the server to
## acknowledge leaving.
func leave() -> void:
	await _core.leave()


## Asks the server for permission to leave.
##
## The server emits [signal NetwMultiplayer.disconnect_requested] and decides
## whether to honor it.
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	_core.request_leave(reason)


## Notifies all clients that the server is shutting down.
##
## Clients receive [signal NetwMultiplayer.server_disconnecting]. The notice
## rides [constant NetwFrameEnvelope.Channel.SESSION_SHUTDOWN] on
## [method SessionCore.notify_shutdown], so a root-installed session with no
## [MultiplayerTree] warns its clients through its own verb.
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	_core.notify_shutdown(reason)

#endregion

#region ── Suspension ──────────────────────────────────────────────────────────

## Pauses the game on every peer via [code]get_tree().paused = true[/code].
##
## The pause is sent to each connected peer individually. Every peer receives
## [signal NetwMultiplayer.tree_paused].
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	_core.pause(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## Every peer receives [signal NetwMultiplayer.tree_unpaused].
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	_core.unpause()

#endregion

#region ── Policy ──────────────────────────────────────────────────────────────

## Overrides the join admission handler for this session, which decides whether
## an arriving [JoinPayload] is admitted and quantizes what it carries.
func set_join_handler(handler: Callable, quantizers: Array = []) -> void:
	_core.set_join_handler(handler, quantizers)


## Overrides the [NetwAuthFlow] this session authenticates arriving peers with.
func set_auth_flow(flow: NetwAuthFlow) -> void:
	_core.set_auth_flow(flow)


## Overrides the provider that answers a probe with this session's
## [NetwServerInfo], so a browser reads the host's own advertised state.
func set_server_info_provider(provider: Callable) -> void:
	_core.set_server_info_provider(provider)

#endregion
