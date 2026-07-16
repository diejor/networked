## Drives establishment for one [NetwMultiplayer] session.
##
## A [NetwConnector] turns a [NetwConnectTarget] or [NetwHostConfig] into a live
## peer by matching it against the process-global, priority-ordered transport
## registry and running a [NetwConnectAttempt]. Transports are stateless, so the
## registry is shared across every session, while each connector owns its own
## attempt and resolves a [NetwPeerView] for the assigned peer. A connector must
## be pumped by its owner through [method poll], since a [RefCounted] has no
## [method Node._process].
## [codeblock]
## var connector := NetwConnector.new(multiplayer)
## var attempt := connector.join(target)
## attempt.progress.connect(_on_progress)
## var result: NetwConnectResult = await attempt.finished
## [/codeblock]
class_name NetwConnector
extends RefCounted

## Emitted when a new attempt begins.
signal attempt_started(attempt: NetwConnectAttempt)

## Emitted when the resolved peer view changes.
signal view_changed(view: NetwPeerView)

# Process-global, priority-ordered registry. Transports are stateless, so global
# registration is safe across the debugger's cloned sessions.
static var _transports: Array[NetwTransport] = []

## Per-instance transport override. When non-empty, it replaces the global
## registry for this connector, which lets a test register isolated transports.
var transports: Array[NetwTransport] = []

## Link conditions for lag and packet loss simulation.
var link_conditions: NetwLinkConditions

## The attempt in flight, or [code]null[/code] when idle.
var current_attempt: NetwConnectAttempt


# The owning session. Weakly held because the owner holds the connector.
var _api_ref: WeakRef
var _peer_view: NetwPeerView

# The attempt awaiting a CONNECTING transport, and its remaining timeout seconds.
# A negative deadline means the transport declared itself self-managed.
var _connecting_attempt: NetwConnectAttempt
var _connect_deadline := 0.0
var _connecting_transport: NetwTransport


## Registers [param transport] in the process-global registry.
##
## First matching [method NetwTransport._can_join] or [method NetwTransport._can_host]
## wins, so [param at_front] gives a transport priority over earlier registrations.
static func add_transport(
		transport: NetwTransport,
		at_front := false,
) -> void:
	if transport in _transports:
		return
	if at_front:
		_transports.push_front(transport)
	else:
		_transports.push_back(transport)


## Removes [param transport] from the process-global registry.
static func remove_transport(transport: NetwTransport) -> void:
	_transports.erase(transport)


## Returns the process-global transport registry in priority order.
static func get_transports() -> Array[NetwTransport]:
	return _transports


# Registers the built-in transports once, when the class first loads, so a bare
# connector resolves the shipped schemes with no setup. Transports are stateless,
# so a single shared instance of each is correct. A game registers its own
# transports, or overrides per instance through [member transports], on top of
# these. The web-only WebRTC loopback is registered by the web entry point, not
# here, since it claims the same scheme as the tracker transport. The directory
# backed transports hold no SDK state and resolve a clean error when their
# lobby directory service is absent, so they register unconditionally too.
static func _static_init() -> void:
	add_transport(ENetTransport.new())
	add_transport(WebSocketTransport.new())
	add_transport(LocalTransport.new())
	add_transport(TrackerWebRTCTransport.new())
	add_transport(NakamaTransport.new())
	add_transport(SteamTransport.new())


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null


## The session this connector drives, while it remains live.
func api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## The view resolved for the currently assigned peer.
##
## Never [code]null[/code] while a peer is assigned, because the generic
## [NetwPeerView] is the guaranteed fallback.
var peer_view: NetwPeerView:
	get:
		return _peer_view


## Begins a join attempt to [param target].
func join(
		target: NetwConnectTarget,
		payload: JoinPayload = null,
		join_args: Array = [],
) -> NetwConnectAttempt:
	var attempt := NetwConnectAttempt.new()
	attempt.target = target
	_begin(attempt)
	_drive_join(attempt, payload, join_args)
	return attempt


## Begins a host attempt for [param config].
func host(
		config: NetwHostConfig,
		payload: JoinPayload = null,
) -> NetwConnectAttempt:
	var attempt := NetwConnectAttempt.new()
	_begin(attempt)
	_drive_host(attempt, config, payload)
	return attempt


## Probes [param target] then joins, or hosts [param config] directly.
##
## A transport without [constant NetwTransport.Capability.LISTEN_FALLBACK] hosts
## directly. Otherwise the connector probes and joins a reachable server or hosts
## when none answers.
func join_or_host(
		target: NetwConnectTarget,
		config: NetwHostConfig,
		payload: JoinPayload = null,
) -> NetwConnectAttempt:
	var attempt := NetwConnectAttempt.new()
	attempt.target = target
	_begin(attempt)
	_drive_join_or_host(attempt, target, config, payload)
	return attempt


# Drives the probe-and-join-or-host fallback path.
func _drive_join_or_host(
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
		config: NetwHostConfig,
		payload: JoinPayload,
) -> void:
	var transport := _resolve_join(target)
	if transport == null:
		attempt.resolve(NetwConnectResult.error(
			"No transport recognizes scheme '%s'." % target.scheme,
		))
		return

	var has_fallback := (transport._capabilities() & NetwTransport.Capability.LISTEN_FALLBACK) != 0
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
	else:
		var a := api()
		var desired_role := a.session.desired_role if a else NetwSessionInterface.Role.LISTEN_SERVER
		if desired_role == NetwSessionInterface.Role.CLIENT:
			var mt := a.tree if a else null
			if mt == null:
				_drive_host(attempt, config, payload)
				return

			var server := mt.duplicate() as MultiplayerTree
			server.desired_role = NetwSessionInterface.Role.DEDICATED_SERVER
			server.name = "Server"
			server.auto_host_headless = false
			mt.get_parent().add_child.call_deferred(server)

			var loop := Engine.get_main_loop() as SceneTree
			if loop:
				await loop.process_frame
			if attempt.is_done():
				server.queue_free.call_deferred()
				return

			var client_sm := mt.get_service(MultiplayerSceneManager)
			if client_sm:
				var server_sm := server.get_service(MultiplayerSceneManager)
				if server_sm:
					for path in client_sm.get_configured_paths():
						server_sm._configure_default(path)

			var host_err: Error = OK
			if server.has_method(&"_open_host"):
				host_err = await server._open_host(true)
			else:
				if server.api == null:
					await server.ready
				var server_connector := NetwConnector.new(server.api)
				var host_attempt := server_connector.host(config, null)
				if not host_attempt.is_done():
					await host_attempt.finished
				var host_res: NetwConnectResult = host_attempt.result
				if not host_res.is_ok():
					host_err = ERR_CANT_CREATE

			if attempt.is_done():
				server.queue_free.call_deferred()
				return

			if host_err == OK:
				var join_addr := ""
				if server.api and server.api.connect and server.api.connect.peer_view:
					join_addr = server.api.connect.peer_view.join_address()

				if join_addr.is_empty():
					join_addr = target.address

				var join_target := NetwConnectTarget.new()
				join_target.scheme = target.scheme
				join_target.address = join_addr
				join_target.display_name = target.display_name
				join_target.metadata = target.metadata

				attempt.target = join_target
				_drive_join(attempt, payload, [])
			elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
				server.queue_free.call_deferred()
				_drive_join(attempt, payload, [])
			else:
				server.queue_free.call_deferred()
				attempt.resolve(NetwConnectResult.error("Embedded server host failed (%s)." % error_string(host_err)))
		else:
			_drive_host(attempt, config, payload)


## Probes [param target] for its [NetwServerInfo] without joining.
##
## Resolves the transport that recognizes [param target] and reads it through
## [method NetwTransport._probe], so [NetwDiscovery] shares the connector's
## transport registry for probing. A scheme no transport recognizes resolves
## [method NetwProbeResult.unsupported].
func probe(target: NetwConnectTarget) -> NetwProbeResult:
	if target == null:
		return NetwProbeResult.error("null target")
	var transport := _resolve_join(target)
	if transport == null:
		return NetwProbeResult.unsupported()
	return await transport._probe(target)


## Pumps the current attempt and peer view for [param dt] seconds.
func poll(dt: float) -> void:
	if _peer_view:
		_peer_view.poll(dt)
	if _connecting_attempt and _connect_deadline >= 0.0:
		_connect_deadline -= dt
		if _connect_deadline <= 0.0:
			_resolve_connecting(NetwConnectResult.timed_out("Connection timed out."))


# Runs a join through PREPARING -> CONSTRUCTING -> CONNECTING.
func _drive_join(
		attempt: NetwConnectAttempt,
		payload: JoinPayload,
		_join_args: Array,
) -> void:
	var transport := _resolve_join(attempt.target)
	if transport == null:
		attempt.resolve(NetwConnectResult.error(
			"No transport recognizes scheme '%s'." % attempt.target.scheme,
		))
		return
	if not await _prepare(attempt, payload):
		return
	attempt.state = NetwConnectAttempt.State.CONSTRUCTING
	attempt.report(&"constructing", "Opening connection...", 0.4)
	var peer: MultiplayerPeer = await transport._join(attempt, attempt.target)
	if attempt.is_done():
		return
	if peer == null:
		attempt.resolve(NetwConnectResult.unreachable(
			&"NO_PEER", "Transport produced no peer.",
		))
		return
	_enter_connecting(attempt, transport, peer)


# Runs a host through PREPARING -> CONSTRUCTING -> CONNECTING.
func _drive_host(
		attempt: NetwConnectAttempt,
		config: NetwHostConfig,
		payload: JoinPayload,
) -> void:
	var transport := _resolve_host(config)
	if transport == null:
		attempt.resolve(NetwConnectResult.error(
			"No transport recognizes scheme '%s'." % config.scheme,
		))
		return
	if not await _prepare(attempt, payload):
		return
	attempt.state = NetwConnectAttempt.State.CONSTRUCTING
	attempt.report(&"constructing", "Opening server...", 0.4)
	var peer: MultiplayerPeer = await transport._host(attempt, config)
	if attempt.is_done():
		return
	if peer == null:
		attempt.resolve(NetwConnectResult.error("Transport produced no host peer."))
		return
	# A live host peer resolves the attempt here rather than in _enter_connecting,
	# so the host player is admitted before the caller sees the session online.
	var live := _enter_connecting(attempt, transport, peer, true)
	await _admit_host_player(payload)
	if live and not attempt.is_done():
		attempt.resolve(NetwConnectResult.ok())


# Admits the host's own player once the session is online, so a kit-driven host
# with a [JoinPayload] plays like the tree's host verb did. Servers never
# auto-submit on [constant NetwSessionInterface.State.ONLINE], so this is the
# host counterpart to the client's prepared-join auto-submit. A payload-less host
# (a dedicated server) admits no local player.
func _admit_host_player(payload: JoinPayload) -> void:
	if payload == null:
		return
	var a := api()
	if a == null or not a.is_server():
		return
	if a.session.state != NetwSessionInterface.State.ONLINE:
		return
	var manager := a.scenes.manager if a.scenes else null
	if manager != null:
		# Startup scenes spawn deferred on becoming server. Wait for them so the
		# host player spawns into a live scene, mirroring the tree's host_ready
		# gate. Bounded so a manager that already spawned never hangs.
		await _await_startup_scenes(manager)
	if a.session.state == NetwSessionInterface.State.ONLINE and a.is_server():
		a.session.submit_join(payload)


# Waits until [param manager] has spawned its startup scenes so the host player
# is admitted into a live scene rather than the default presentation. Returns at
# once when the manager already spawned (a re-host), otherwise waits for the
# spawn signal or the scenes to appear, capped so a manager that never spawns
# cannot hang the host.
func _await_startup_scenes(manager: MultiplayerSceneManager) -> void:
	if not manager.active_scenes.is_empty():
		return
	var loop := Engine.get_main_loop() as SceneTree
	if loop == null:
		return
	var fired := [false]
	var cb := func() -> void: fired[0] = true
	manager.startup_scenes_spawned.connect(cb, CONNECT_ONE_SHOT)
	var guard := 0
	while not fired[0] and manager.active_scenes.is_empty() and guard < 600:
		await loop.process_frame
		guard += 1
	if manager.startup_scenes_spawned.is_connected(cb):
		manager.startup_scenes_spawned.disconnect(cb)


# Runs the PREPARING stage, awaiting the session's credential preparation.
# Returns false when the attempt resolved with a failure.
func _prepare(attempt: NetwConnectAttempt, payload: JoinPayload) -> bool:
	attempt.state = NetwConnectAttempt.State.PREPARING
	attempt.report(&"preparing", "Preparing...", 0.1)
	var a := api()
	if payload != null and a:
		var err: Error = await a.session.prepare_join(payload)
		if err != OK:
			attempt.resolve(NetwConnectResult.error(
				"Join preparation failed (%s)." % error_string(err),
			))
			return false
	return not attempt.is_done()


# Assigns the built peer (the session edge) and observes the connection.
#
# Returns true when the peer was live at assignment, which a synchronous host
# always is. A [param defer_resolve] caller owns the terminal resolve so it can
# admit its host player first, otherwise a live peer resolves the attempt here.
func _enter_connecting(
		attempt: NetwConnectAttempt,
		transport: NetwTransport,
		peer: MultiplayerPeer,
		defer_resolve: bool = false,
) -> bool:
	attempt.state = NetwConnectAttempt.State.CONNECTING
	attempt.report(&"connecting", "Connecting...", 0.7)
	attempt.view = _resolve_view(transport, peer, attempt)
	_set_peer_view(attempt.view)
	var final_peer := peer
	if link_conditions:
		final_peer = link_conditions.wrap_peer(peer)
	var a := api()
	if a:
		a.multiplayer_peer = final_peer
	if final_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
		if not defer_resolve:
			attempt.resolve(NetwConnectResult.ok())
		return true
	_connecting_attempt = attempt
	_connecting_transport = transport
	_connect_deadline = transport._timeout_hint(attempt.target)
	if a:
		a.connected_to_server.connect(_on_connecting_succeeded, CONNECT_ONE_SHOT)
		a.connection_failed.connect(_on_connecting_failed, CONNECT_ONE_SHOT)
	attempt.finished.connect(_on_connecting_resolved, CONNECT_ONE_SHOT)
	return false


# Resolves the connecting attempt with a terminal result and clears the binds.
func _resolve_connecting(result: NetwConnectResult) -> void:
	var attempt := _connecting_attempt
	if attempt == null:
		return
	attempt.resolve(result)


func _on_connecting_succeeded() -> void:
	# The server sits at peer id 1, so a view-backed transport can attach its
	# per-connection diagnostics (WebRTC candidate stats) to the success result.
	var diags := { }
	if _connecting_attempt and _connecting_attempt.view:
		diags = _connecting_attempt.view.diagnostics(1)
	_resolve_connecting(NetwConnectResult.ok(diags))


func _on_connecting_failed() -> void:
	_resolve_connecting(NetwConnectResult.unreachable(
		&"PEER_CONNECT_FAILED", "Could not reach the server.",
	))


# Clears the connecting state once the attempt resolves for any reason.
func _on_connecting_resolved(_result: NetwConnectResult) -> void:
	var a := api()
	if a:
		if a.connected_to_server.is_connected(_on_connecting_succeeded):
			a.connected_to_server.disconnect(_on_connecting_succeeded)
		if a.connection_failed.is_connected(_on_connecting_failed):
			a.connection_failed.disconnect(_on_connecting_failed)
	_connecting_attempt = null
	_connecting_transport = null
	_connect_deadline = 0.0


# Resolves the view for an assigned peer, falling back to the generic view.
func _resolve_view(
		transport: NetwTransport,
		peer: MultiplayerPeer,
		attempt: NetwConnectAttempt,
) -> NetwPeerView:
	var view := transport._make_view(peer, attempt)
	return view if view else NetwPeerView.new(peer)


# Installs the resolved view and announces the change.
func _set_peer_view(view: NetwPeerView) -> void:
	if _peer_view == view:
		return
	if _peer_view:
		_peer_view.close()
	_peer_view = view
	view_changed.emit(view)


# Returns the active transport list, preferring the per-instance override.
func _active_transports() -> Array[NetwTransport]:
	return transports if not transports.is_empty() else _transports


# Returns the first registered transport that recognizes [param target].
func _resolve_join(target: NetwConnectTarget) -> NetwTransport:
	for transport in _active_transports():
		if transport._can_join(target):
			return transport
	return null


# Returns the first registered transport that recognizes [param config].
func _resolve_host(config: NetwHostConfig) -> NetwTransport:
	for transport in _active_transports():
		if transport._can_host(config):
			return transport
	return null


# Tracks the new attempt and announces it.
func _begin(attempt: NetwConnectAttempt) -> void:
	attempt._api_ref = _api_ref
	current_attempt = attempt
	attempt_started.emit(attempt)
