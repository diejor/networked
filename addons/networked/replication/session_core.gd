## The session interface for one [MultiplayerTree], owned by [NetwMultiplayer].
##
## The session always has one, and it holds session truth only: what state the
## session is in, what [enum Role] the local peer plays, who was admitted, and
## what the roster says. Opening a connection is nobody's business here. Peer
## assignment drives the machine instead:
## [method NetwMultiplayer._set_multiplayer_peer] calls
## [method on_peer_assigned] right after handing the peer to the wrapped
## [SceneMultiplayer], so the session reacts to the one edge every host, join,
## test rig, and embedded-server path already crosses. That is what lets a bare
## API with no owning [MultiplayerTree] still answer [member state] and
## [member role], and it is what keeps the whole connect kit outside this file.
## [codeblock]
## OFFLINE ─assign peer─▶ CONNECTING ─success─▶ ONLINE
##    ▲                       │                    │
##    └─── null / offline ────┘   leave / crash ───┤
##    │                                            ▼
##    └──────────────────────────────────── DISCONNECTING
## [/codeblock]
## The machine itself is [NetwSessionCore], and this file is what surrounds it:
## the authentication wire, the join payloads and their codec, the frames the
## session sends, and the persistence flush a leave owes.
## [codeblock]
## NetwSessionCore   the states and their edges, the roles, the flood guard
## SessionCore       auth, join payloads, the frames, the awaits
## [/codeblock]
## Every engine signal is forwarded through the matching signal here, so a
## consumer binds to the session it already had.
class_name SessionCore
extends RefCounted

const Async := preload("res://addons/networked/utils/async.gd")

const SessionRoster := preload("res://addons/networked/session/session_roster.gd")

## Session lifecycle state.
## [codeblock]
## OFFLINE
##   | assign a peer
##   v
## CONNECTING --(failure or cancel)--> OFFLINE
##   | success
##   v
## ONLINE
##   | leave
##   v
## DISCONNECTING --> OFFLINE
## [/codeblock]
enum State {
	## No active [MultiplayerPeer] is assigned.
	OFFLINE = NetwSessionCore.STATE_OFFLINE,
	## A peer is assigned and the connection is being established.
	CONNECTING = NetwSessionCore.STATE_CONNECTING,
	## The session has an active [MultiplayerPeer] and configured services.
	ONLINE = NetwSessionCore.STATE_ONLINE,
	## The session is closing the active peer and clearing its state.
	DISCONNECTING = NetwSessionCore.STATE_DISCONNECTING,
}

## Runtime role the local peer plays in the current session.
enum Role {
	## No session role has been assigned yet.
	NONE = NetwSessionCore.ROLE_NONE,
	## Connected to a remote server as a client.
	CLIENT = NetwSessionCore.ROLE_CLIENT,
	## Hosts the session without acting as a local client.
	DEDICATED_SERVER = NetwSessionCore.ROLE_DEDICATED_SERVER,
	## Hosts the session and also represents the local player.
	LISTEN_SERVER = NetwSessionCore.ROLE_LISTEN_SERVER,
}

## Emitted when [member state] changes.
##
## The edge doubles as the notification that a peer was assigned, which
## [MultiplayerAPI] never surfaces on its own. A consumer that wants to observe
## "a peer just changed" binds here.
signal state_changed(old_state: State, new_state: State)

## Emitted on entering [constant State.ONLINE], once the peer is
## live and [member role] is set.
signal session_entered()

## Emitted on leaving [constant State.ONLINE]. Never fires on a
## connect that failed before [constant State.ONLINE].
signal session_ended()

## Emitted after a join is admitted and its [ResolvedJoin] remembered, once per
## peer, before [signal NetwMultiplayer.participant_joined]. The session owner
## binds here to run its own admission side effects, such as binding the local
## participant or spawning a player, without the session machine knowing what
## those are.
signal participant_admitted(peer_id: int)

## Emitted when server authority pauses the game.
signal paused(reason: String)

## Emitted when server authority unpauses the game.
signal unpaused()

## Emitted when server authority kicks this peer.
signal kicked(reason: String)

## The machine this interface surrounds: the states, their legal edges, the
## roles, and the join flood window.
var core := NetwSessionCore.new()

## The current connection state of the session.
##
## Assigning it only moves the value and announces the edge. Drive it through
## [method transition] so the enter and exit hooks run on each edge.
var state: State:
	get:
		return core.state as State
	set(new_state):
		core.state = new_state as NetwSessionCore.State

## The role the local peer plays in the current session, assigned by the
## connect flow before it reaches [constant State.ONLINE].
var role: Role:
	get:
		return core.role as Role
	set(new_role):
		core.role = new_role as NetwSessionCore.Role

## Admission policy for an inbound join, or an unset [Callable] for open join.
##
## Called on the server with the deserialized [JoinPayload] and the sender peer
## id, and returns the admitted [ResolvedJoin] or [code]null[/code] to reject.
## An unset gate runs the session's own default: resolve the identity, reject an
## invalid payload, and reject the loser of a username collision. A session that
## authenticates against a service sets its own.
var join_gate: Callable

## The game-build tag that gates admission, from the registered
## [member NetwSessionConfig.app_id]. Empty until a [MultiplayerTree] registers.
var app_id: StringName:
	get:
		return _config.app_id

## The [enum Role] the local peer intends to play, from the
## registered [member NetwSessionConfig.desired_role]. This is the hint the
## assignment edge reads to pick the server role.
var desired_role: Role:
	get:
		# The config authors this in the public enum, which mirrors Role exactly.
		return int(_config.desired_role)

## The configuration this session was registered with, or the default one until
## an embedding registers its own. Never [code]null[/code], which is what lets a
## bare session answer [member app_id] and [member link_conditions] with
## defaults instead of a special inert mode.
var config: NetwSessionConfig:
	get:
		return _config

## The player cap the live host advertises, or zero while this session is not
## hosting.
##
## Whatever opened the host stamps the cap it resolved, so
## [method NetwServerInfo.from_session] answers a probe with a plain session
## fact rather than reading back the configuration the host was built from.
var advertised_max_players: int:
	get:
		return core.advertised_max_players
	set(value):
		core.advertised_max_players = value

# The session configuration, a default until a MultiplayerTree registers its
# NetwSessionConfig. Facts read through it so a bare API answers with defaults
# instead of a special inert mode.
var _config: NetwSessionConfig = NetwSessionConfig.new()

# Authentication is session wire state, so it lives beside the session machine
# and binds straight to the wrapped SceneMultiplayer. The tree only supplies a
# typed config and optional admission policy.
var _auth: AuthCoordinator

# A successfully prepared join waiting for the next client ONLINE edge. It is
# consumed before submission so repeated connection signals cannot resend it.
var _prepared_join: JoinPayload

# A submitted client join held for one resend, in case the first submit dropped
# at the carrier because the server peer had not yet landed in get_peers.
var _resubmit_join: JoinPayload

# The built-in join handler, bound lazily to this session's api. Resolved when no
# project registration or per-session override is present.
var _default_join: NetwDefaultJoin

# Per-session join handler override and its wire-arg quantizers, taking
# precedence over the project-wide Netw.configure_join registration. For tests,
# the debugger, or a session that genuinely differs.
var _join_override: Callable
var _join_override_quantizers: Array = []

# The auth flow constructed once from the project-wide Netw.configure_auth
# factory, cached for this session's lifetime. A per-session override takes
# precedence, for tests, the debugger, or a service binding a runtime flow.
var _bound_auth_flow: NetwAuthFlow
var _auth_flow_override: NetwAuthFlow

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	_auth = AuthCoordinator.new(api._roster if api else SessionRoster.new())
	# The machine decides the edges and this interface is what the session's
	# consumers connect to, so every engine signal is forwarded. The two hooks
	# that are not pure announcements ride the same edges: an entered session
	# submits a client's prepared join, and an offline one drops it.
	core.state_changed.connect(_on_core_state_changed)
	core.session_entered.connect(_on_core_session_entered)
	core.session_ended.connect(session_ended.emit)
	if api:
		_auth.bind_api(api.inner)
		_auth.set_owner(api)
		# A client peer is still mid-handshake at assignment, so the connect
		# completes on the relayed connection signals rather than at the edge. A
		# failed handshake returns to OFFLINE without ever entering ONLINE, and a
		# server that vanishes mid-session ends it through the same teardown a
		# graceful leave takes.
		api.connected_to_server.connect(_on_inner_connected)
		api.connection_failed.connect(_on_inner_connect_failed)
		api.server_disconnected.connect(_on_server_dropped)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## Applies [param config] as the session configuration.
##
## Called by [NetwMultiplayer] when a [MultiplayerTree] registers its
## [NetwSessionConfig] through [method MultiplayerAPI.object_configuration_add].
func configure(config: NetwSessionConfig) -> void:
	_config = config
	_push_desired_role()
	_apply_auth_config()


## Restores the default configuration when the registering [MultiplayerTree]
## removes its [NetwSessionConfig].
func deconfigure() -> void:
	_config = NetwSessionConfig.new()
	_push_desired_role()
	_apply_auth_config()


# The hint the machine splits a server peer with. Pushed rather than read,
# because the machine holds no configuration, and pushed again at every edge
# that can resolve a role, because a config is authored live and the value it
# carried at registration is not the one that decides.
func _push_desired_role() -> void:
	core.desired_role = desired_role as NetwSessionCore.Role


## Overrides the probe reply for this one session with [param provider], taking
## precedence over any [method Netw.configure_server_info] registration. Tests,
## the debugger, and a session that genuinely differs use it. An invalid
## [Callable] clears the override.
func set_server_info_provider(provider: Callable) -> void:
	_auth.set_server_info_provider(provider)


## Rebinds session wire hooks after [member NetwMultiplayer.inner] changes.
func adopt_inner(inner: SceneMultiplayer) -> void:
	_auth.bind_api(inner)
	_apply_auth_config()


## Sets the application-defined authentication callback.
##
## Networked keeps ownership of the wrapped callback so probe packets stay
## internal. All other authentication packets are delegated to [param callback].
func set_auth_callback(callback: Callable) -> void:
	_auth.set_application_auth_callback(callback)


## Releases session wire hooks during [method NetwEmbeddingHandle.dispose].
func dispose() -> void:
	_clear_prepared_join()
	_auth.clear()


## Prepares [param payload] for the next connection.
##
## Validates the local join identity, awaits the configured provider's
## credential preparation, and stores the payload used to build credentials
## during Godot's auth phase. A client consumes and submits it when the session
## reaches [constant State.ONLINE]. A host keeps it until scene readiness and an
## explicit [method submit_join]. This does not assign a transport peer.
func prepare_join(payload: JoinPayload) -> Error:
	_clear_prepared_join()
	if payload == null:
		Netw.dbg.error("join_payload is null.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER
	if payload.username.is_empty():
		Netw.dbg.error("username is empty.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER

	_auth.prepare()
	var prepare_err := await _auth.prepare_join_payload(payload)
	if prepare_err != OK:
		return prepare_err
	_auth.set_client_join_payload(payload)
	_prepared_join = payload
	return OK


# Applies the registered session facts to the auth wire engine. The dispatcher
# stays armed because every Networked session uses the base hello protocol and
# same-port probes share its isolated authentication phase.
func _apply_auth_config() -> void:
	_auth.set_auth_flow(_effective_auth_flow())
	_auth.set_app_tag(_compute_app_tag(_config.app_id))
	_auth.prepare()

## The auth flow bound to this session: a per-session override, else the flow
## constructed from the project-wide [method Netw.configure_auth] factory, else
## [code]null[/code] (open admission). A service binding a runtime flow reads it
## to attach its session and tree.
var auth_flow: NetwAuthFlow:
	get:
		return _effective_auth_flow()


## Overrides the auth flow for this one session with [param flow], taking
## precedence over any [method Netw.configure_auth] factory. Tests, the debugger,
## and a runtime-bound service flow use it.
func set_auth_flow(flow: NetwAuthFlow) -> void:
	_auth_flow_override = flow
	_auth.set_auth_flow(_effective_auth_flow())


# Resolves the effective flow: a per-session override, then a cached instance
# from the project-wide factory, else null.
func _effective_auth_flow() -> NetwAuthFlow:
	if _auth_flow_override != null:
		return _auth_flow_override
	if _bound_auth_flow == null:
		var factory := Netw.resolve_auth_factory()
		if factory.is_valid():
			var api := _api()
			if api != null:
				_bound_auth_flow = factory.call(api)
	return _bound_auth_flow


# Folds the build tag into the 32-bit value carried by the hello. Empty means
# the compatibility gate is disabled.
func _compute_app_tag(value: StringName) -> int:
	return NetwSessionCore.compute_app_tag(value)


## Advances [member state] to [param next] along a legal edge, running the exit
## hook for the old state then the enter hook for the new one.
##
## The only entry point allowed to move [member state], so setup and teardown
## stay paired.
func transition(next: State) -> void:
	core.transition(next as NetwSessionCore.State)


## Reacts to a peer handed to the session by
## [method NetwMultiplayer._set_multiplayer_peer].
##
## A live transport peer drives the connect direction straight from the edge, so
## a bare [code]multiplayer_peer = peer[/code] with no host or join verb still
## reaches [constant State.ONLINE]. A server peer is live at
## assignment. A client peer is still mid-handshake, so it waits in
## [constant State.CONNECTING] until the transport reports the
## connection. A [code]null[/code] or [OfflineMultiplayerPeer] assignment while
## connecting collapses the machine back to
## [constant State.OFFLINE], so a cancelled connect becomes this
## edge rather than a bespoke abort verb. A [code]null[/code] assignment while
## already [constant State.ONLINE] is left alone, since a tree deletion nulls the
## peer that way and must not end the session here. A graceful leave and a server
## crash own the [constant State.ONLINE] teardown instead.
func on_peer_assigned(peer: MultiplayerPeer) -> void:
	# The peer is read here and the decision is taken below it, because the
	# machine holds no Object. An [OfflineMultiplayerPeer] is not a live peer,
	# which is how a cancelled connect arrives as this edge rather than as a
	# bespoke abort verb.
	var live := peer != null and not peer is OfflineMultiplayerPeer
	_push_desired_role()
	core.on_peer_assigned(
		live,
		live and peer.get_connection_status() \
			== MultiplayerPeer.CONNECTION_CONNECTED,
		peer.get_unique_id() if live else 0,
	)


# Completes a client connect once the transport reports it reached the server.
func _on_inner_connected() -> void:
	if state != State.CONNECTING:
		return
	_push_desired_role()
	_resolve_online()


# A failed handshake returns to OFFLINE without ever entering ONLINE.
func _on_inner_connect_failed() -> void:
	if state == State.CONNECTING:
		transition(State.OFFLINE)


# Ends the session when the transport reports the server vanished. A crash
# arrives while ONLINE and reuses the leave path's DISCONNECTING -> OFFLINE
# teardown. A leave has already moved to DISCONNECTING, and a disposing api that
# closes its own peer sets the embedding disposing, so neither is mistaken for
# a crash.
func _on_server_dropped() -> void:
	var api := _api()
	if api and api.embedding.is_disposing():
		return
	if state != State.ONLINE:
		return
	transition(State.DISCONNECTING)
	transition(State.OFFLINE)


# Enters ONLINE with the role the peer identity and the config hint imply.
func _resolve_online() -> void:
	var api := _api()
	core.resolve_online(api.get_unique_id() if api else 1)


# The announcement, then the setup half the machine cannot own, which is the
# half that needs the wire. A client submits the join it prepared. A server has
# no handshake coming to bring it an identity, so it makes its own.
func _on_core_session_entered() -> void:
	session_entered.emit()
	if role == Role.CLIENT:
		_submit_prepared_join()
	else:
		_auth.synthesize_host_identity()


# Every edge, forwarded, and the teardown half that owns a payload rather than
# a state: a session back at OFFLINE is no longer holding a join to send.
func _on_core_state_changed(old_state: int, new_state: int) -> void:
	state_changed.emit(old_state as State, new_state as State)
	if new_state == State.OFFLINE:
		_clear_prepared_join()


## Explicitly submits [param payload] as the local player's join request.
##
## The request rides [constant NetwFrameEnvelope.Channel.SESSION_JOIN] on the
## carrier rather than a node [code]@rpc[/code], so a session with no
## [MultiplayerTree] still joins. A host submits to itself locally, matching the
## call-local path the tree rpc took. Clients normally submit their prepared
## payload automatically on [constant State.ONLINE]. This method remains for
## rejoin and custom flows.
##
## [br][br][b]Player request.[/b]
func submit_join(payload: JoinPayload) -> void:
	var api := _api()
	if api == null or payload == null:
		return
	if payload == _prepared_join:
		_prepared_join = null
		_auth.set_client_join_payload(null)
	_encode_join_args(payload)
	if api.is_server():
		_handle_join_frame(payload.serialize(), 1)
	else:
		api._replication.send_to(
			1,
			0,
			NetwFrameEnvelope.Channel.SESSION_JOIN,
			payload.serialize(),
			true,
		)


## Flushes persistence, closes the active peer, and returns to
## [constant State.OFFLINE].
func leave() -> void:
	if state == State.OFFLINE:
		return
	var api := _api()
	if api == null:
		return

	Netw.dbg.trace("SessionCore: leave called.")
	Netw.dbg.info("Disconnecting player.")
	api._persistence.flush_all()
	transition(State.DISCONNECTING)
	if api.has_multiplayer_peer():
		api.multiplayer_peer.close()

	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree:
		var timer := scene_tree.create_timer(3.0)
		await Async.timeout(api.server_disconnected, timer)
	transition(State.OFFLINE)
	advertised_max_players = 0


## Pauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	var api := _api()
	assert(_is_server_role(), "pause() must be called on server authority.")
	for peer_id: int in api.get_peers():
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_PAUSE,
			var_to_bytes(reason),
			true,
		)
	_handle_pause_frame(var_to_bytes(reason), 1)


## Unpauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	var api := _api()
	assert(_is_server_role(), "unpause() must be called on server authority.")
	for peer_id: int in api.get_peers():
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_UNPAUSE,
			PackedByteArray(),
			true,
		)
	_handle_unpause_frame(1)


## Disconnects [param peer_id] from the session.
##
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	var api := _api()
	assert(_is_server_role(), "kick() must be called on server authority.")
	if not reason.is_empty():
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_KICKED,
			var_to_bytes(reason),
			true,
		)
	if api.has_multiplayer_peer():
		api.multiplayer_peer.disconnect_peer(peer_id)


## Warns every peer that the server is shutting down.
##
## The notice rides [constant NetwFrameEnvelope.Channel.SESSION_SHUTDOWN] rather
## than a node [code]@rpc[/code], so a root-installed session with no
## [MultiplayerTree] still warns its clients before it tears down. Each recipient
## fires [signal NetwMultiplayer.server_disconnecting].
##
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	var api := _api()
	assert(_is_server_role(), "notify_shutdown() must be called on server authority.")
	for peer_id: int in api.get_peers():
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_SHUTDOWN,
			var_to_bytes(reason),
			true,
		)
	_handle_shutdown_frame(var_to_bytes(reason), 1)


## Asks the server to kick [param peer_id].
##
## The request rides [constant NetwFrameEnvelope.Channel.SESSION_KICK_REQUEST]
## rather than a node [code]@rpc[/code], so a session with no [MultiplayerTree]
## still asks. The server fires [signal NetwMultiplayer.kick_requested] and
## decides. A host asking submits to itself locally.
##
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	var api := _api()
	if api == null:
		return
	var payload := var_to_bytes([peer_id, reason])
	if api.is_server():
		_handle_kick_request_frame(payload, 1)
	else:
		api._replication.send_to(
			1,
			0,
			NetwFrameEnvelope.Channel.SESSION_KICK_REQUEST,
			payload,
			true,
		)


## Asks the server for permission to leave.
##
## The request rides [constant NetwFrameEnvelope.Channel.SESSION_LEAVE_REQUEST].
## The server fires [signal NetwMultiplayer.disconnect_requested] and decides.
##
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	var api := _api()
	if api == null:
		return
	var payload := var_to_bytes(reason)
	if api.is_server():
		_handle_leave_request_frame(payload, 1)
	else:
		api._replication.send_to(
			1,
			0,
			NetwFrameEnvelope.Channel.SESSION_LEAVE_REQUEST,
			payload,
			true,
		)


# Consumes the prepared client join before sending it. Clearing first makes the
# ONLINE edge idempotent even when the transport repeats its connected signal.
# A relayed transport can raise connected_to_server a poll before the server peer
# lands in get_peers, and the carrier drops a send to a peer it cannot yet see, so
# that first submit is resent once peer_connected reports the server. A transport
# whose server peer is already present arms nothing, since its peer_connected
# preceded the online edge, and the join lands on this first submit.
func _submit_prepared_join() -> void:
	var payload := _prepared_join
	if payload == null:
		return
	_prepared_join = null
	_auth.set_client_join_payload(null)
	submit_join(payload)
	var api := _api()
	if api and MultiplayerPeer.TARGET_PEER_SERVER not in api.get_peers():
		_resubmit_join = payload
		if not api.peer_connected.is_connected(_resubmit_join_on_server_peer):
			api.peer_connected.connect(_resubmit_join_on_server_peer)


# Resends the client join once the server peer connects, covering the relayed
# transport whose connected_to_server outran its server peer registration. The
# first submit dropped at the carrier, so this is the only delivery, not a double.
func _resubmit_join_on_server_peer(peer_id: int) -> void:
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		return
	var api := _api()
	if api and api.peer_connected.is_connected(_resubmit_join_on_server_peer):
		api.peer_connected.disconnect(_resubmit_join_on_server_peer)
	var payload := _resubmit_join
	_resubmit_join = null
	if payload:
		submit_join(payload)


# Drops the pending join frame, the credentials derived from it, and a resubmit
# still waiting on the server peer.
func _clear_prepared_join() -> void:
	_prepared_join = null
	_auth.set_client_join_payload(null)
	_resubmit_join = null
	var api := _api()
	if api and api.peer_connected.is_connected(_resubmit_join_on_server_peer):
		api.peer_connected.disconnect(_resubmit_join_on_server_peer)


# Returns whether the resolved session role carries server authority.
func _is_server_role() -> bool:
	return core.is_server_role()


# The admission policy a session runs when nothing registered its own gate:
# resolve the identity, reject an invalid payload, and reject the loser of a
# username collision. This is the permissive baseline, so an unauthenticated
# session still cannot admit two players under one name.
func _default_join_gate(join_payload: JoinPayload, peer_id: int) -> ResolvedJoin:
	var rj := join_payload.resolve()
	if rj == null:
		Netw.dbg.warn("join: invalid payload from peer %d", [peer_id])
		return null
	var api := _api()
	if api == null:
		return null
	if not api._roster.resolve_username_collision(
		rj,
		api.players,
		api.inner.disconnect_peer,
	):
		return null
	return rj


# Server receive for a join request. Resolves it through join_gate (or open
# resolve), admits it, tells every peer, and backfills the new peer's roster.
func _handle_join_frame(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	if _join_flooded(sender):
		return
	var join_payload := JoinPayload.new()
	join_payload.deserialize(payload)
	join_payload.peer_id = sender
	if not _decode_join_args(join_payload):
		Netw.dbg.warn(
			"join: rejected malformed or mismatched args from peer %d",
			[sender],
		)
		return
	_auth.resolve_identity(sender, join_payload)

	var rj: ResolvedJoin = join_gate.call(join_payload, sender) \
	if join_gate.is_valid() \
	else _default_join_gate(join_payload, sender)
	if rj == null:
		return

	_admit(rj)
	for peer_id: int in api.get_peers():
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_ACCEPT,
			rj.serialize(),
			true,
		)
	if sender != 1:
		api._replication.send_to(
			sender,
			0,
			NetwFrameEnvelope.Channel.SESSION_ROSTER,
			var_to_bytes(api._roster.serialize_accepted_joins()),
			true,
		)


# Whether sender's join frames exceed the flood window. The wall clock is read
# here and handed down, so the machine's window is a function of its arguments.
func _join_flooded(sender: int) -> bool:
	return core.join_flooded(sender, Time.get_ticks_msec())


## Overrides the join handler for this one session with [param handler] and its
## wire-arg [param quantizers], taking precedence over any
## [method Netw.configure_join] registration. Tests and the debugger use it. An
## invalid [Callable] restores the resolved default.
func set_join_handler(handler: Callable, quantizers: Array = []) -> void:
	_join_override = handler
	_join_override_quantizers = quantizers


# Resolves this session's join handler: a per-session override, then the
# project-wide registration, then the built-in NetwDefaultJoin bound to this api.
func _resolve_join_handler() -> Callable:
	if _join_override.is_valid():
		return _join_override
	var registered := Netw.resolve_join_handler()
	if registered.is_valid():
		return registered
	if _default_join == null:
		var api := _api()
		if api == null:
			return Callable()
		_default_join = NetwDefaultJoin.new(api)
	return _default_join.spawn


# The wire-arg quantizers matching the resolved handler.
func _resolve_join_quantizers() -> Array:
	if _join_override.is_valid():
		return _join_override_quantizers
	if Netw.resolve_join_handler().is_valid():
		return Netw.resolve_join_quantizers()
	return []


# Packs a client's typed arg_values into arg_bytes plus a schema hash. No intent
# or no resolvable handler leaves empty bytes.
func _encode_join_args(payload: JoinPayload) -> void:
	var handler := _resolve_join_handler()
	NetwJoinCodec.encode(payload, handler, _resolve_join_quantizers())


# Verifies and decodes arg_bytes into arg_values against this server's resolved
# handler. Returns false to reject a schema mismatch or an undecodable request.
func _decode_join_args(payload: JoinPayload) -> bool:
	var handler := _resolve_join_handler()
	return NetwJoinCodec.decode(
		payload,
		handler,
		_resolve_join_quantizers(),
	)


## Invokes the resolved join handler once for the accepted [param participant],
## passing the decoded join args after the [ResolvedJoin]. A returned
## [NetwSceneHandle] becomes the participant's
## [member NetwParticipant.current_scene]. A participant with no join intent
## spawns nothing.
##
## [br][br][b]Server Only.[/b]
func run_join_handler(participant: NetwParticipant) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	var rj := participant.join
	if rj == null or rj.arg_values.is_empty():
		return
	var handler := _resolve_join_handler()
	if not handler.is_valid():
		return
	var scene = await handler.callv([rj] + rj.arg_values)
	var record := NetwEntity.of(scene) if scene is Node else null
	if record != null:
		participant.current_scene = record.scene


# Client receive for one accepted participant, server to every peer.
func _handle_accept_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	_admit(ResolvedJoin.deserialize(payload))


# Client receive for the roster backfill sent to a newly accepted peer.
func _handle_roster_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var payloads: Array = bytes_to_var(payload)
	for bytes: PackedByteArray in payloads:
		_admit(ResolvedJoin.deserialize(bytes))


# Applies a server pause notification locally.
func _handle_pause_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree:
		scene_tree.paused = true
	paused.emit(str(bytes_to_var(payload)))


# Applies a server unpause notification locally.
func _handle_unpause_frame(sender: int) -> void:
	if sender != 1:
		return
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree:
		scene_tree.paused = false
	unpaused.emit()


# Announces a server kick notification before the transport closes.
func _handle_kicked_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	kicked.emit(str(bytes_to_var(payload)))


# Fires the local shutdown notice from a server SESSION_SHUTDOWN frame.
func _handle_shutdown_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var api := _api()
	if api:
		api.server_disconnecting.emit(str(bytes_to_var(payload)))


# Server receive for a client kick request. The frame sender is the requester;
# the server decides whether to honor it through kick_requested.
func _handle_kick_request_frame(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	var data: Variant = bytes_to_var(payload)
	if not data is Array or (data as Array).size() != 2:
		return
	api.kick_requested.emit(sender, int(data[0]), str(data[1]))


# Server receive for a client leave request, decided through disconnect_requested.
func _handle_leave_request_frame(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	api.disconnect_requested.emit(sender, str(bytes_to_var(payload)))


# Remembers an accepted join and announces it once. The owner's admission side
# effects run off participant_admitted before the public participant_joined.
func _admit(rj: ResolvedJoin) -> void:
	var api := _api()
	if api == null or rj == null:
		return
	if not api._roster.remember_accepted_join(rj):
		return
	var participant := api.peer_get_participant(rj.peer_id)
	if participant == null:
		return
	participant_admitted.emit(rj.peer_id)
	# The session runs the join handler on admit, so a root-installed session
	# with no MultiplayerTree still spawns joiners. Server-guarded inside.
	if api.is_server():
		run_join_handler(participant)
	if rj.peer_id == api.get_unique_id():
		api.local_participant_joined.emit(participant)
	api.participant_joined.emit(participant)
