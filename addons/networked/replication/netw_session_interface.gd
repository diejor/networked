## The session lifecycle machine for one [MultiplayerTree], owned by
## [NetwMultiplayer].
##
## [member NetwMultiplayer.session] is never [code]null[/code]. Peer assignment
## drives the machine: [method NetwMultiplayer._set_multiplayer_peer] calls
## [method on_peer_assigned] right after handing the peer to the wrapped
## [SceneMultiplayer], so the session reacts to the one edge every host, join,
## test rig, and embedded-server path already crosses. The session's truth lives
## here rather than on the tree that configures it, which is what lets a bare API
## with no owning [MultiplayerTree] still answer [member state] and [member role].
## [codeblock]
## OFFLINE ─assign peer─▶ CONNECTING ─success─▶ ONLINE
##    ▲                       │                    │
##    └─── null / offline ────┘   leave / crash ───┤
##    │                                            ▼
##    └──────────────────────────────────── DISCONNECTING
## [/codeblock]
class_name NetwSessionInterface
extends RefCounted

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
	OFFLINE,
	## A peer is assigned and the connection is being established.
	CONNECTING,
	## The session has an active [MultiplayerPeer] and configured services.
	ONLINE,
	## The session is closing the active peer and clearing its state.
	DISCONNECTING,
}

## Runtime role the local peer plays in the current session.
enum Role {
	## No session role has been assigned yet.
	NONE,
	## Connected to a remote server as a client.
	CLIENT,
	## Hosts the session without acting as a local client.
	DEDICATED_SERVER,
	## Hosts the session and also represents the local player.
	LISTEN_SERVER,
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

# Every legal [enum State] edge keyed by source state.
# [method transition] hard-asserts against this so every entry and exit path
# converges on the same transitions instead of one per caller. The server-crash
# route reuses ONLINE -> DISCONNECTING -> OFFLINE rather than a direct edge.
const _LEGAL_EDGES := {
	State.OFFLINE: [State.CONNECTING],
	State.CONNECTING: [
		State.ONLINE,
		State.OFFLINE,
	],
	State.ONLINE: [State.DISCONNECTING],
	State.DISCONNECTING: [State.OFFLINE],
}

## The current connection state of the session.
##
## This member only stores the value and emits [signal state_changed]. Drive it
## through [method transition] so the enter and exit hooks run on each edge.
var state: State = State.OFFLINE:
	set(new_state):
		if state == new_state:
			return
		var old := state
		state = new_state
		state_changed.emit(old, new_state)

## The role the local peer plays in the current session, assigned by the
## connect flow before it reaches [constant State.ONLINE].
var role: Role = Role.NONE

## Admission policy for an inbound join, or an unset [Callable] for open join.
##
## Called on the server with the deserialized [JoinPayload] and the sender peer
## id, and returns the admitted [ResolvedJoin] or [code]null[/code] to reject. An
## unset gate resolves the payload and admits every join, the permissive
## baseline a session with no configured authenticator runs. A session owner that
## authenticates or gates on username collision sets this.
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
		return _config.desired_role

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

# Per-peer join-frame timestamps for the flood guard. An honest peer submits one
# join, twice at most through the resend path, so a small window drops a malicious
# flood without touching legitimate multi-client bursts. Pruned when it exceeds
# _JOIN_TRACKED_PEERS so a long-lived server never accumulates idle peers.
const _JOIN_RATE_LIMIT := 8
const _JOIN_TRACKED_PEERS := 64
var _join_stamps: Dictionary = { }

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
	_apply_auth_config()


## Restores the default configuration when the registering [MultiplayerTree]
## removes its [NetwSessionConfig].
func deconfigure() -> void:
	_config = NetwSessionConfig.new()
	_apply_auth_config()


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


## Releases session wire hooks during [method NetwMultiplayer.dispose].
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
	if String(value).is_empty():
		return 0
	return String(value).hash() & 0xFFFFFFFF


## Advances [member state] to [param next] along a legal edge, running the exit
## hook for the old state then the enter hook for the new one.
##
## The only entry point allowed to move [member state], so setup and teardown
## stay paired.
func transition(next: State) -> void:
	if state == next:
		return
	assert(
		next in _LEGAL_EDGES[state],
		"Illegal session transition %s -> %s." % [
			State.keys()[state],
			State.keys()[next],
		],
	)
	var prev := state
	_on_exit_state(prev)
	state = next
	_on_enter_state(next)


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
	if peer == null or peer is OfflineMultiplayerPeer:
		if state == State.CONNECTING:
			transition(State.OFFLINE)
		return
	if state == State.OFFLINE:
		transition(State.CONNECTING)
	if state != State.CONNECTING:
		return
	if peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
		_resolve_online()


# Completes a client connect once the transport reports it reached the server.
func _on_inner_connected() -> void:
	if state == State.CONNECTING:
		_resolve_online()


# A failed handshake returns to OFFLINE without ever entering ONLINE.
func _on_inner_connect_failed() -> void:
	if state == State.CONNECTING:
		transition(State.OFFLINE)


# Ends the session when the transport reports the server vanished. A crash
# arrives while ONLINE and reuses the leave path's DISCONNECTING -> OFFLINE
# teardown. A leave has already moved to DISCONNECTING, and a disposing api that
# closes its own peer sets is_disposing(), so neither is mistaken for a crash.
func _on_server_dropped() -> void:
	var api := _api()
	if api and api.is_disposing():
		return
	if state != State.ONLINE:
		return
	transition(State.DISCONNECTING)
	transition(State.OFFLINE)


# Enters ONLINE with the role the peer identity and the config hint imply. A
# unique id of 1 is the server, split into listen and dedicated by the hint.
func _resolve_online() -> void:
	var api := _api()
	var uid := api.get_unique_id() if api else 1
	if uid == 1:
		role = Role.LISTEN_SERVER \
		if desired_role == Role.LISTEN_SERVER \
		else Role.DEDICATED_SERVER
	else:
		role = Role.CLIENT
	transition(State.ONLINE)
	if role != Role.CLIENT:
		_auth.synthesize_host_identity()


# Runs the setup half on entering a state. ONLINE finalizes the live session.
func _on_enter_state(next: State) -> void:
	match next:
		State.OFFLINE:
			_clear_prepared_join()
		State.ONLINE:
			session_entered.emit()
			if role == Role.CLIENT:
				_submit_prepared_join()


# Runs the teardown half on leaving a state. Leaving ONLINE ends the session so
# it never fires on a failed connect (CONNECTING -> OFFLINE).
func _on_exit_state(prev: State) -> void:
	match prev:
		State.ONLINE:
			session_ended.emit()


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
		api.replication.send_to(
			1,
			0,
			NetwFrameEnvelope.Channel.SESSION_JOIN,
			payload.serialize(),
			true,
		)


## Opens a listen or dedicated host over [param config] through the connect kit
## and brings the session to [constant State.ONLINE].
##
## A host has no [signal MultiplayerAPI.connected_to_server] edge, so
## [method on_peer_assigned] only auto-resolves a listen peer that already
## reports [constant MultiplayerPeer.CONNECTION_CONNECTED] at assignment (an ENet
## server). A transport whose peer reports connecting at assignment (the
## in-process loopback bus) is finished here instead, so a root-installed session
## with no owning [MultiplayerTree] hosts through its own verb rather than
## delegating to a tree it does not have.
## [codeblock]
## var config := NetwHostConfig.new()
## config.scheme = &"enet"
## var err := await api.session.open_host(config)
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func open_host(config: NetwHostConfig) -> Error:
	var api := _api()
	if api == null:
		return ERR_UNCONFIGURED
	assert(state == State.OFFLINE, "Must be offline to host.")
	if config == null or String(config.scheme).is_empty():
		Netw.dbg.error(
			"open_host: no transport scheme configured.",
			[],
			func(m): push_error(m),
		)
		return ERR_UNCONFIGURED

	var attempt := api.connect.connector().host(config, null)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res == null or not res.is_ok():
		if state == State.CONNECTING:
			transition(State.OFFLINE)
		return ERR_CANT_CREATE

	# The connector assigns the listen peer, so on_peer_assigned may already have
	# resolved a CONNECTED peer online. A peer still connecting at assignment
	# leaves the machine in CONNECTING with no client edge to finish it, and a
	# connector that reported ok without a peer edge leaves it OFFLINE. Both are
	# driven to ONLINE here.
	if state == State.OFFLINE:
		transition(State.CONNECTING)
	if state == State.CONNECTING:
		_resolve_online()
	return OK


## Flushes persistence, closes the active peer, and returns to
## [constant State.OFFLINE].
func leave() -> void:
	if state == State.OFFLINE:
		return
	var api := _api()
	if api == null:
		return

	Netw.dbg.trace("NetwSessionInterface: leave called.")
	Netw.dbg.info("Disconnecting player.")
	api.persistence.flush_all()
	transition(State.DISCONNECTING)
	if api.has_multiplayer_peer():
		api.multiplayer_peer.close()

	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree:
		var timer := scene_tree.create_timer(3.0)
		await Async.timeout(api.server_disconnected, timer)
	transition(State.OFFLINE)


## Pauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	var api := _api()
	assert(_is_server_role(), "pause() must be called on server authority.")
	for peer_id: int in api.get_peers():
		api.replication.send_to(
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
		api.replication.send_to(
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
		api.replication.send_to(
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
		api.replication.send_to(
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
		api.replication.send_to(
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
		api.replication.send_to(
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
	return role == Role.DEDICATED_SERVER or role == Role.LISTEN_SERVER


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
	else join_payload.resolve()
	if rj == null:
		return

	_admit(rj)
	for peer_id: int in api.get_peers():
		api.replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_ACCEPT,
			rj.serialize(),
			true,
		)
	if sender != 1:
		api.replication.send_to(
			sender,
			0,
			NetwFrameEnvelope.Channel.SESSION_ROSTER,
			var_to_bytes(api._roster.serialize_accepted_joins()),
			true,
		)


# Whether sender's join frames exceed the flood window. The host self-join at
# peer 1 carries authority and is never limited.
func _join_flooded(sender: int) -> bool:
	if sender == 1:
		return false
	var now := Time.get_ticks_msec()
	var window_start := now - 1000
	if _join_stamps.size() > _JOIN_TRACKED_PEERS:
		_prune_join_stamps(window_start)
	var stamps: Array = _join_stamps.get_or_add(sender, [] as Array[int])
	while not stamps.is_empty() and stamps[0] < window_start:
		stamps.pop_front()
	stamps.push_back(now)
	return stamps.size() > _JOIN_RATE_LIMIT


# Drops peers whose newest join fell out of the current window.
func _prune_join_stamps(window_start: int) -> void:
	for peer_id: int in _join_stamps.keys():
		var stamps: Array = _join_stamps[peer_id]
		if stamps.is_empty() or stamps[stamps.size() - 1] < window_start:
			_join_stamps.erase(peer_id)


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


# The handler's wire schema: its parameter types after the framework-owned
# ResolvedJoin.
func _join_arg_types(handler: Callable) -> Array:
	var obj := handler.get_object()
	var script: Script = obj.get_script() if obj else null
	var types := NetwScriptModel.get_method_arg_types(script, handler.get_method())
	return types.slice(1) if not types.is_empty() else []


# A stable identity of the wire schema (arg types plus quantizer classes), so a
# client and server that disagree on the handler signature reject each other.
func _join_schema_hash(handler: Callable, quantizers: Array) -> int:
	var sig := ""
	for t: int in _join_arg_types(handler):
		sig += str(t) + ","
	sig += "|"
	for q: NetwQuantize in quantizers:
		sig += (q.get_class() if q != null else "_") + ","
	return sig.hash()


# Packs a client's typed arg_values into arg_bytes plus a schema hash. No intent
# or no resolvable handler leaves empty bytes.
func _encode_join_args(payload: JoinPayload) -> void:
	if payload.arg_values.is_empty():
		payload.arg_bytes = PackedByteArray()
		payload.schema_hash = 0
		return
	var handler := _resolve_join_handler()
	if not handler.is_valid():
		payload.arg_bytes = PackedByteArray()
		payload.schema_hash = 0
		return
	var quantizers := _resolve_join_quantizers()
	var writer := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_values(
		writer,
		payload.arg_values,
		quantizers,
		_join_arg_types(handler),
	)
	payload.arg_bytes = writer.to_bytes()
	payload.schema_hash = _join_schema_hash(handler, quantizers)


# Verifies and decodes arg_bytes into arg_values against this server's resolved
# handler. Returns false to reject a schema mismatch or an undecodable request.
func _decode_join_args(payload: JoinPayload) -> bool:
	if payload.arg_bytes.is_empty():
		payload.arg_values = []
		return true
	var handler := _resolve_join_handler()
	if not handler.is_valid():
		return false
	var quantizers := _resolve_join_quantizers()
	if payload.schema_hash != _join_schema_hash(handler, quantizers):
		return false
	var reader := NetwBitBuffer.Reader.new(payload.arg_bytes)
	payload.arg_values = NetwScriptModel.read_call_args(
		reader,
		quantizers,
		_join_arg_types(handler),
	)
	return true


## Invokes the resolved join handler once for the accepted [param participant],
## passing the decoded join args after the [ResolvedJoin]. A returned
## [MultiplayerScene] becomes the participant's
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
	if scene is MultiplayerScene:
		participant.current_scene = scene


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
	var participant := api.get_participant(rj.peer_id)
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
