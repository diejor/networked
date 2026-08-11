## The bring-up verbs for one session: host, join, probe, and the fallbacks
## between them.
##
## A connector is a client of the session machine, never a wrapper. It builds
## peers and assigns them; the machine decides what the session then [i]is[/i],
## so nothing here re-emits a session signal or forwards a session verb. That
## one-way call keeps every transport, attempt, and view in script space no
## matter where the machine itself ends up living.
## [codeblock]
## var connector := NetwConnector.of(NetwMultiplayer.of(self))
##
## var result := await connector.host(payload)     # or join(target, payload)
## if not result.is_ok():
##     push_warning(result.message)
## [/codeblock]
class_name NetwConnector
extends RefCounted

# The one connector per session, parked on the api itself so it survives a
# config rebuild and a swapped session machine alike.
const _META_KEY := &"_netw_connector"

## Emitted when a bring-up attempt begins.
##
## Progress is per attempt, so an observer connects here once and reads each
## [NetwConnectAttempt] rather than every caller threading one back out of
## [method host] and [method join]. One verb call can raise more than one
## attempt: a host that finds the port taken joins the peer that took it.
signal attempt_started(attempt: NetwConnectAttempt)

## Emitted with the terminal [NetwConnectResult] of a whole verb call.
##
## An outcome is per verb call, which is the grain a failure banner wants: the
## losing attempt of a fallback that then succeeds never reaches here.
signal finished(result: NetwConnectResult)

## Narrows this session to these [NetwTransport]s instead of the process-global
## [method NetwTransport.registered] list.
##
## Empty means the global registry, which is the shipping case. A test registers
## isolated transports here so one rig's fakes never resolve for another session
## in the same process.
var transports: Array[NetwTransport] = []

## The bring-up attempt in flight, or [code]null[/code] while idle.
var current_attempt: NetwConnectAttempt

## The [NetwHostConfig] behind the live hosted peer, or [code]null[/code] while
## this session is not hosting.
var active_host_config: NetwHostConfig

## The [NetwPeerView] resolved for the currently assigned peer.
##
## Never [code]null[/code] while a peer this connector built is assigned,
## because the generic [NetwPeerView] is the guaranteed fallback. The connector
## pumps it on [signal NetwMultiplayer.poll_started] and closes it exactly once
## when a new peer replaces it.
var peer_view: NetwPeerView:
	get:
		return _peer_view

var _peer_view: NetwPeerView

# The attempt awaiting a CONNECTING transport, and its remaining timeout
# seconds. A negative deadline means the transport declared itself self-managed.
var _connecting_attempt: NetwConnectAttempt
var _connect_deadline := 0.0

# Set between abort() and the attempt actually unwinding, so the losing result
# is classified as cancelled rather than as whatever the transport happened to
# report while closing.
var _aborting: bool = false

# The session this connector connects. A weakref because the api owns the
# connector through its meta and both are reference counted.
var _api_ref: WeakRef


## Returns the connector for [param api], building it on first use.
##
## One session has one connector for its whole life, so two callers can never
## drive competing attempts against the same machine. It hangs off the api
## rather than off [SessionCore], which is what keeps a rig that substitutes the
## machine from stranding it.
static func of(api: NetwMultiplayer) -> NetwConnector:
	if api == null:
		return null
	if api.has_meta(_META_KEY):
		return api.get_meta(_META_KEY) as NetwConnector
	var connector := NetwConnector.new(api)
	api.set_meta(_META_KEY, connector)
	return connector


## Returns the [enum @GlobalScope.Error] that reports [param result] to a caller
## whose own contract is an error code, such as [method MultiplayerTree.host].
##
## The status is the classification a connect flow reads. This is the lossy
## projection of it, for a boundary that has no room for one.
static func error_of(result: NetwConnectResult) -> Error:
	if result == null:
		return FAILED
	if result.is_ok():
		return OK
	match result.detail:
		&"UNCONFIGURED":
			return ERR_UNCONFIGURED
		&"INVALID_TARGET":
			return ERR_INVALID_PARAMETER
		&"HOST_FAILED":
			return ERR_CANT_CREATE
	return ERR_CANT_CONNECT


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	if api:
		# Carrier servicing rides the tick ahead of the transport read, so a
		# packet the view releases is read in the frame it arrived.
		api.poll_started.connect(_poll)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null

#region ── Verbs ───────────────────────────────────────────────────────────────

## Starts this session as a host, admitting [param join_payload] as the local
## player.
##
## A null [param join_payload] hosts dedicated: no local identity is prepared
## and none is submitted, which is the whole difference between a listen host
## and a dedicated one. [param config] overrides the registered transport for
## this one call, and omitting it self-sources through
## [method default_host_config]. A host that cannot open joins instead, which is
## what makes a second launch on one machine land in one session.
##
## [br][br][b]Server Only.[/b]
func host(
		join_payload: JoinPayload,
		config: NetwHostConfig = null,
) -> NetwConnectResult:
	var api := _api()
	if api == null:
		return _report(_unconfigured("host: no session."))
	var resolved := config if config != null else default_host_config()
	if resolved == null:
		return _report(_unconfigured("host: no transport configured."))
	if join_payload == null:
		return _report(await _open_host(resolved))

	var prepare_err := await api.session.prepare_join(join_payload)
	if prepare_err != OK:
		return _report(
			NetwConnectResult.error(
				"Join preparation failed (%s)." % error_string(prepare_err),
			),
		)
	await api.session.leave()

	var opened := await _open_host(resolved)
	if opened.is_ok():
		await _admit_host_player(join_payload)
		return _report(opened)
	if opened.detail == &"UNCONFIGURED":
		return _report(opened)
	# The port is somebody else's host, so the local player joins them.
	var rejoin_err := await api.session.prepare_join(join_payload)
	if rejoin_err != OK:
		return _report(
			NetwConnectResult.error(
				"Join preparation failed (%s)." % error_string(rejoin_err),
			),
		)
	return _report(await _join_local_host(join_payload))


## Opens the transport against the [param target] address and submits
## [param join_payload] once connected.
##
## The client peer assigned here drives the machine to
## [constant NetwMultiplayer.SessionState.ONLINE] through the peer-assignment
## and connected-to-server edges, the second of which submits the join prepared
## here. A [param quiet] join reports nothing on failure, which suits a rig that
## expects one.
##
## [br][br][b]Player request.[/b]
func join(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
		quiet: bool = false,
) -> NetwConnectResult:
	return _report(await _join(target, join_payload, quiet))


## Probes [param target]. Joins if a host answers, hosts otherwise.
##
## Whichever edge wins drives the session online and admits the local player, so
## the caller does not learn which one happened from the result. Read
## [member NetwMultiplayer.is_host] afterward. [param config] overrides the
## registered transport for this one call.
##
## [br][br][b]Player request.[/b]
func join_or_host(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
		config: NetwHostConfig = null,
) -> NetwConnectResult:
	var resolved := config if config != null else default_host_config()
	if resolved == null:
		return _report(_unconfigured("join_or_host: no transport configured."))
	if target == null:
		Netw.dbg.error("join_or_host: target is null.", func(m): push_error(m))
		var invalid := NetwConnectResult.error("join_or_host: target is null.")
		invalid.detail = &"INVALID_TARGET"
		return _report(invalid)
	var attempt := _begin_attempt()
	attempt.target = target
	_drive_join_or_host(attempt, target, resolved, join_payload)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res == null:
		res = NetwConnectResult.error("no transport")
	if not res.is_ok():
		# The stage that refused already reported itself, so the verb records
		# the outcome without raising a second error for one failure.
		Netw.dbg.warn("Failed to connect: %s", [res.message])
	return _report(res)


## Probes [param target] for its [NetwServerInfo] without joining.
##
## Resolves the transport that recognizes [param target] out of
## [method transports_in_effect] and reads it through
## [method NetwTransport._probe], so [NetwServerBrowser] shares whatever this
## session resolves against. A scheme no transport recognizes reports
## [method NetwProbeResult.unsupported].
func probe(target: NetwConnectTarget) -> NetwProbeResult:
	if target == null:
		return NetwProbeResult.error("null target")
	var transport_impl := _resolve_join(target)
	if transport_impl == null:
		return NetwProbeResult.unsupported()
	return await transport_impl._probe(target)


## Cancels a bring-up still in flight, the connect-time sibling of
## [method NetwSessionHandle.leave].
##
## The losing attempt resolves [constant NetwConnectResult.Status.ABORTED] even
## when the transport reports a generic error on its way out, so a browser tells
## "you cancelled" apart from "the server refused".
##
## [br][br][b]Player request.[/b]
func abort() -> void:
	_aborting = true
	if current_attempt and not current_attempt.is_done():
		current_attempt.abort()
		_unwind_connecting_peer()


## Returns a [NetwHostConfig] over the registered
## [member NetwSessionConfig.transport], or [code]null[/code] when no embedding
## registered one.
##
## This is what lets an embedding author its transport once and then host with
## no config at all. A caller that passes its own config overrides this for that
## one call.
## [codeblock]
## config.transport registered   ->  connector.host(payload)      # self-sources
## nothing registered            ->  connector.host(payload, cfg) # caller supplies
## neither                       ->  a result whose detail is UNCONFIGURED
## [/codeblock]
func default_host_config() -> NetwHostConfig:
	var api := _api()
	if api == null or api.session.config.transport == null:
		return null
	var config := NetwHostConfig.new()
	config.transport = api.session.config.transport
	return config


## The [NetwTransport]s this session resolves against: [member transports] when
## that is non-empty, and the process-global [method NetwTransport.registered]
## list otherwise.
func transports_in_effect() -> Array[NetwTransport]:
	return transports if not transports.is_empty() \
	else NetwTransport.registered()

#endregion

#region ── Servicing ───────────────────────────────────────────────────────────

# Services the carrier and counts the connect deadline down, ahead of the
# transport read. A headless client joining a dead host resolves on its own
# rather than awaiting a finish that never comes.
func _poll(dt: float) -> void:
	if _peer_view:
		_peer_view.poll(dt)
	if _connecting_attempt and _connect_deadline >= 0.0:
		_connect_deadline -= dt
		if _connect_deadline <= 0.0:
			_resolve_connecting(NetwConnectResult.timed_out("Connection timed out."))

#endregion

#region ── Driving ─────────────────────────────────────────────────────────────

# The join half of the public verb, without the per-verb outcome report, so a
# fallback can reuse it inside one verb call.
func _join(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
		quiet: bool,
) -> NetwConnectResult:
	var api := _api()
	if api == null:
		return _unconfigured("join: no session.")
	# A payload-less join opens the transport without an identity, which is what
	# a rig exercising establishment alone does. Preparing here rather than only
	# inside the attempt is what lets a rejected identity surface as its own
	# failure instead of a generic connect one.
	if join_payload != null:
		var prepare_err := await api.session.prepare_join(join_payload)
		if prepare_err != OK:
			return NetwConnectResult.error(
				"Join preparation failed (%s)." % error_string(prepare_err),
			)
	var attempt := _begin_attempt()
	attempt.target = target
	_drive_join(attempt, join_payload, [])
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res == null:
		res = NetwConnectResult.error("no transport")
	if not res.is_ok() and not quiet:
		Netw.dbg.error("Failed to join: %s", [res.message])
	return res


# Opens a listen or dedicated host over config and waits for the machine to
# answer. The peer assignment is the whole edge: a server peer is born live, so
# the machine resolves it online without a nudge from here.
func _open_host(config: NetwHostConfig) -> NetwConnectResult:
	var api := _api()
	if api == null:
		return _unconfigured("host: no session.")
	assert(api.state == NetwMultiplayer.SessionState.OFFLINE, "Must be offline to host.")
	if config == null or String(config.scheme).is_empty():
		Netw.dbg.error(
			"host: no transport scheme configured.",
			[],
			func(m): push_error(m),
		)
		return _unconfigured("host: no transport scheme configured.")

	var attempt := _begin_attempt()
	_drive_host(attempt, config, null)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res == null or not res.is_ok():
		_unwind_connecting_peer()
		var failed := res if res else NetwConnectResult.error("Host failed.")
		failed.detail = &"HOST_FAILED"
		return failed
	if not api.is_online:
		_unwind_connecting_peer()
		var stalled := NetwConnectResult.error(
			"The host peer never came online.",
		)
		stalled.detail = &"HOST_FAILED"
		return stalled
	return res


# Tracks a new attempt and announces it.
func _begin_attempt() -> NetwConnectAttempt:
	var attempt := NetwConnectAttempt.new(_api())
	current_attempt = attempt
	attempt_started.emit(attempt)
	return attempt


# Runs a join through PREPARING -> CONSTRUCTING -> CONNECTING.
func _drive_join(
		attempt: NetwConnectAttempt,
		payload: JoinPayload,
		_join_args: Array,
) -> void:
	# A joining peer is not hosting, so a stale host config never leaks into a
	# later session's probe replies.
	_clear_advertised_host()
	var transport_impl := _resolve_join(attempt.target)
	if transport_impl == null:
		attempt.resolve(
			NetwConnectResult.error(
				"No transport recognizes scheme '%s'." % attempt.target.scheme,
			),
		)
		return
	if not await _prepare_attempt(attempt, payload):
		return
	attempt.state = NetwConnectAttempt.State.CONSTRUCTING
	attempt.report(&"constructing", "Opening connection...", 0.4)
	var peer: MultiplayerPeer = await transport_impl._join(attempt, attempt.target)
	if attempt.is_done():
		return
	if peer == null:
		attempt.resolve(
			NetwConnectResult.unreachable(
				&"NO_PEER",
				"Transport produced no peer.",
			),
		)
		return
	_enter_connecting(attempt, transport_impl, peer)


# Runs a host through PREPARING -> CONSTRUCTING -> CONNECTING.
func _drive_host(
		attempt: NetwConnectAttempt,
		config: NetwHostConfig,
		payload: JoinPayload,
) -> void:
	var transport_impl := _resolve_host(config)
	if transport_impl == null:
		attempt.resolve(
			NetwConnectResult.error(
				"No transport recognizes scheme '%s'." % config.scheme,
			),
		)
		return
	if not await _prepare_attempt(attempt, payload):
		return
	attempt.state = NetwConnectAttempt.State.CONSTRUCTING
	attempt.report(&"constructing", "Opening server...", 0.4)
	var peer: MultiplayerPeer = await transport_impl._host(attempt, config)
	if attempt.is_done():
		return
	if peer == null:
		attempt.resolve(NetwConnectResult.error("Transport produced no host peer."))
		return
	_advertise_host(config)
	# A live host peer resolves the attempt here rather than in
	# _enter_connecting, so the host player is admitted before the caller sees
	# the session online.
	var live := _enter_connecting(attempt, transport_impl, peer, true)
	await _admit_host_player(payload)
	if live and not attempt.is_done():
		attempt.resolve(NetwConnectResult.ok())


# Drives the probe-and-join-or-host fallback path.
func _drive_join_or_host(
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
		config: NetwHostConfig,
		payload: JoinPayload,
) -> void:
	var transport_impl := _resolve_join(target)
	if transport_impl == null:
		attempt.resolve(
			NetwConnectResult.error(
				"No transport recognizes scheme '%s'." % target.scheme,
			),
		)
		return

	var has_fallback := (transport_impl._capabilities() \
					& NetwTransport.Capability.LISTEN_FALLBACK) != 0
	if not has_fallback:
		_drive_host(attempt, config, payload)
		return

	attempt.state = NetwConnectAttempt.State.PREPARING
	attempt.report(&"probing", "Probing for existing server...", 0.2)
	var probe_res := await probe(target)
	if attempt.is_done():
		return

	if probe_res.is_ok():
		_drive_join(attempt, payload, [])
		return
	var api := _api()
	if api == null or api.session.config.desired_role != NetwMultiplayer.Role.CLIENT:
		_drive_host(attempt, config, payload)
		return

	var mt := api.root as MultiplayerTree
	if mt == null:
		_drive_host(attempt, config, payload)
		return
	await _join_raised_sibling(mt, attempt, target, config, payload)


# Stands a dedicated sibling up and joins it, the CLIENT desired-role path. The
# node work is the tree's, the bring-up is this connector's, and the sibling
# gets its own connector because it is its own session.
func _join_raised_sibling(
		mt: MultiplayerTree,
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
		config: NetwHostConfig,
		payload: JoinPayload,
) -> void:
	var server := await mt.raise_embedded_server()
	if server == null:
		attempt.resolve(NetwConnectResult.error("Could not raise a server."))
		return
	if attempt.is_done():
		server.queue_free.call_deferred()
		return

	var host_result := await NetwConnector.of(server.api).host(null, config)
	if attempt.is_done():
		server.queue_free.call_deferred()
		return
	if not host_result.is_ok():
		server.queue_free.call_deferred()
		if host_result.detail != &"HOST_FAILED":
			attempt.resolve(
				NetwConnectResult.error(
					"Embedded server host failed (%s)." % host_result.message,
				),
			)
			return
		# Somebody else already holds the port, so join them instead.
		_drive_join(attempt, payload, [])
		return

	var join_target := NetwConnectTarget.new()
	join_target.scheme = target.scheme
	join_target.address = NetwConnector.of(server.api).join_address()
	if join_target.address.is_empty():
		join_target.address = target.address
	join_target.display_name = target.display_name
	join_target.metadata = target.metadata
	attempt.target = join_target
	_drive_join(attempt, payload, [])


# Joins a same-machine host over this session's registered transport, used by
# the host-with-local-player fallback.
func _join_local_host(
		join_payload: JoinPayload,
		address: String = "",
) -> NetwConnectResult:
	var api := _api()
	if address.is_empty():
		address = _loopback_join_address()
	var target := NetwConnectTarget.new()
	var params := api.session.config.transport if api else null
	target.scheme = params._scheme() if params else &""
	target.address = address
	# The authored parameters travel with the target, so a host that falls back
	# to joining reaches the same port it tried to open rather than the
	# transport's default one.
	if params:
		target.metadata = params.to_dict()
	return await _join(target, join_payload, false)


## Returns the address others use to join this host, or [code]""[/code] when the
## transport surfaces none. Reads [member peer_view].
func join_address() -> String:
	return _peer_view.join_address() if _peer_view else ""


# The loopback address a same-machine host is listening on, falling back to
# localhost when the view surfaces none.
func _loopback_join_address() -> String:
	var addr := join_address()
	return addr if not addr.is_empty() else "localhost"


# Admits the host's own player once the session is online, so a kit-driven host
# with a JoinPayload plays like a client that joined. Servers never auto-submit
# on ONLINE, so this is the host counterpart to the client's prepared-join
# auto-submit. A payload-less host (a dedicated server) admits no local player.
func _admit_host_player(payload: JoinPayload) -> void:
	if payload == null:
		return
	var api := _api()
	if api == null or not api.is_server():
		return
	if api.state != NetwMultiplayer.SessionState.ONLINE:
		return
	if api._scenes.has_declaration():
		# Startup scenes spawn deferred on becoming server. Wait for them so the
		# host player spawns into a live scene. Bounded so a session that already
		# spawned never hangs.
		await _await_startup_scenes(api._scenes)
	if api.state == NetwMultiplayer.SessionState.ONLINE and api.is_server():
		api.session.submit_join(payload)


# Waits until the session has spawned its startup scenes, capped so a session
# that never spawns cannot hang the host.
func _await_startup_scenes(scene_api: SceneCore) -> void:
	if not scene_api.scenes.is_empty():
		return
	var loop := Engine.get_main_loop() as SceneTree
	if loop == null:
		return
	var fired := [false]
	var cb := func() -> void: fired[0] = true
	scene_api._startup_scenes_spawned.connect(cb, CONNECT_ONE_SHOT)
	var guard := 0
	while not fired[0] and scene_api.scenes.is_empty() and guard < 600:
		await loop.process_frame
		guard += 1
	if scene_api._startup_scenes_spawned.is_connected(cb):
		scene_api._startup_scenes_spawned.disconnect(cb)


# Runs the PREPARING stage, awaiting the session's credential preparation.
# Returns false when the attempt resolved with a failure.
func _prepare_attempt(attempt: NetwConnectAttempt, payload: JoinPayload) -> bool:
	attempt.state = NetwConnectAttempt.State.PREPARING
	attempt.report(&"preparing", "Preparing...", 0.1)
	if payload != null:
		var api := _api()
		var err: Error = await api.session.prepare_join(payload) if api \
		else ERR_UNCONFIGURED
		if err != OK:
			attempt.resolve(
				NetwConnectResult.error(
					"Join preparation failed (%s)." % error_string(err),
				),
			)
			return false
	return not attempt.is_done()


# Assigns the built peer (the session edge) and observes the connection.
#
# Returns true when the peer was live at assignment, which a synchronous host
# always is. A defer_resolve caller owns the terminal resolve so it can admit
# its host player first, otherwise a live peer resolves the attempt here.
func _enter_connecting(
		attempt: NetwConnectAttempt,
		transport_impl: NetwTransport,
		peer: MultiplayerPeer,
		defer_resolve: bool = false,
) -> bool:
	attempt.state = NetwConnectAttempt.State.CONNECTING
	attempt.report(&"connecting", "Connecting...", 0.7)
	attempt.view = _resolve_view(transport_impl, peer, attempt)
	_set_peer_view(attempt.view)
	var api := _api()
	var final_peer := peer
	# The session's own configured link conditions wrap the peer, so a tree-less
	# session simulates the latency it was authored with.
	var conditions := api.session.config.link_conditions if api else null
	if conditions:
		final_peer = conditions.wrap_peer(peer)
	if api:
		api.multiplayer_peer = final_peer
	if final_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
		if not defer_resolve:
			attempt.resolve(NetwConnectResult.ok())
		return true
	_connecting_attempt = attempt
	_connect_deadline = transport_impl._timeout_hint(attempt.target)
	if api:
		api.connected_to_server.connect(_on_connecting_succeeded, CONNECT_ONE_SHOT)
		api.connection_failed.connect(_on_connecting_failed, CONNECT_ONE_SHOT)
	attempt.finished.connect(_on_connecting_resolved, CONNECT_ONE_SHOT)
	return false


# Resolves the connecting attempt with a terminal result, classifying an abort
# this connector asked for as cancelled rather than as whatever the transport
# reported on its way out.
func _resolve_connecting(result: NetwConnectResult) -> void:
	var attempt := _connecting_attempt
	if attempt == null:
		return
	attempt.resolve(_classify_result(result))
	_unwind_connecting_peer()


# Restamps a terminal result so an aborted attempt reads ABORTED.
func _classify_result(result: NetwConnectResult) -> NetwConnectResult:
	if result == null or result.is_ok() or not _aborting:
		return result
	return NetwConnectResult.aborted(result.message)


# Hands the machine its cancel edge after an attempt died mid-connect. An
# OfflineMultiplayerPeer is the one edge every unwind crosses, so a timeout or
# an abort leaves no session stuck in CONNECTING behind a dead peer.
func _unwind_connecting_peer() -> void:
	var api := _api()
	if api == null or api.state != NetwMultiplayer.SessionState.CONNECTING:
		return
	api.multiplayer_peer = OfflineMultiplayerPeer.new()


func _on_connecting_succeeded() -> void:
	# The server sits at peer id 1, so a view-backed transport can attach its
	# per-connection diagnostics (WebRTC candidate stats) to the success result.
	var diags := { }
	if _connecting_attempt and _connecting_attempt.view:
		diags = _connecting_attempt.view.diagnostics(1)
	_resolve_connecting(NetwConnectResult.ok(diags))


func _on_connecting_failed() -> void:
	_resolve_connecting(
		NetwConnectResult.unreachable(
			&"PEER_CONNECT_FAILED",
			"Could not reach the server.",
		),
	)


# Clears the connecting state once the attempt resolves for any reason.
func _on_connecting_resolved(_result: NetwConnectResult) -> void:
	var api := _api()
	if api:
		if api.connected_to_server.is_connected(_on_connecting_succeeded):
			api.connected_to_server.disconnect(_on_connecting_succeeded)
		if api.connection_failed.is_connected(_on_connecting_failed):
			api.connection_failed.disconnect(_on_connecting_failed)
	_connecting_attempt = null
	_connect_deadline = 0.0
	_aborting = false


# Resolves the view for an assigned peer, falling back to the generic view.
func _resolve_view(
		transport_impl: NetwTransport,
		peer: MultiplayerPeer,
		attempt: NetwConnectAttempt,
) -> NetwPeerView:
	var view := transport_impl._make_view(peer, attempt)
	return view if view else NetwPeerView.new(peer)


# Installs the resolved view and closes the one it replaces.
func _set_peer_view(view: NetwPeerView) -> void:
	if _peer_view == view:
		return
	if _peer_view:
		_peer_view.close()
	_peer_view = view


# Returns the first resolvable transport that recognizes target.
func _resolve_join(target: NetwConnectTarget) -> NetwTransport:
	for transport_impl in transports_in_effect():
		if transport_impl._can_join(target):
			return transport_impl
	return null


# Returns the first resolvable transport that recognizes config.
func _resolve_host(config: NetwHostConfig) -> NetwTransport:
	for transport_impl in transports_in_effect():
		if transport_impl._can_host(config):
			return transport_impl
	return null


# Publishes what this host opened with, so a probe reply carries the cap.
func _advertise_host(config: NetwHostConfig) -> void:
	active_host_config = config
	var api := _api()
	if api == null:
		return
	if config.max_players > 0:
		api.session.advertised_max_players = config.max_players
		return
	# An unset config cap advertises the transport's own resolved one, which
	# ENet writes back into its params after create_server picks it.
	var enet := config.transport as NetwENetParams
	api.session.advertised_max_players = enet.max_clients if enet else 0


func _clear_advertised_host() -> void:
	active_host_config = null
	var api := _api()
	if api:
		api.session.advertised_max_players = 0


# A failure a caller could only have prevented by configuring a transport.
func _unconfigured(message: String) -> NetwConnectResult:
	var result := NetwConnectResult.error(message)
	result.detail = &"UNCONFIGURED"
	return result


# Announces the outcome of a whole verb call and returns it unchanged.
func _report(result: NetwConnectResult) -> NetwConnectResult:
	var out := result if result else NetwConnectResult.error("no result")
	finished.emit(out)
	return out

#endregion
