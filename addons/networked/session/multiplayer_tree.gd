@icon("res://addons/networked/assets/MultiplayerTree.svg")
@tool
class_name MultiplayerTree
extends Node
## Root node for one Networked session.
##
## [member api], [member desired_role], [member role],
## [member state], and the session service registry all
## belong to this tree. Child networked nodes resolve this owner through
## [method resolve].
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "PlayerOne"
##
## # Start as a player host.
## var host_err := await tree.host_player(payload)
##
## # Or connect to a known server.
## var target := NetwConnectTarget.new()
## target.scheme = &"enet"
## target.address = "127.0.0.1"
## var join_err := await tree.join(target, payload)
## [/codeblock]

## Emitted when [member api], [member role], and session services are ready.
##
## Fires on entering [constant NetwSessionInterface.State.ONLINE], so it pairs with
## [signal session_ended] and the two strictly alternate. A connect attempt that
## fails before [constant NetwSessionInterface.State.ONLINE] emits neither, so a subscriber that wires
## session state here can rely on exactly one matching [signal session_ended].
signal session_entered()

## Emitted when the tree leaves [constant NetwSessionInterface.State.ONLINE] and tears down the
## session.
##
## Fires on exiting [constant NetwSessionInterface.State.ONLINE] through either
## [method leave] or the server-crash path, never on a failed
## connect. Session-scoped subscribers that wired up on [signal session_entered]
## release their connections and replication state here so nothing accumulates
## across repeated sessions on the same tree.
signal session_ended()

## Emitted when a new peer connects to the server.
signal peer_connected(peer_id: int)

## Emitted when a peer disconnects from the server.
signal peer_disconnected(peer_id: int)

## Emitted on the client when it successfully connects to the server.
signal connected_to_server()

## Emitted on the client when the server disconnects or crashes.
signal server_disconnected()

## Emitted once for each accepted participant known to this peer.
##
## Fresh accepts emit on every peer. Late joiners also receive one emission per
## participant accepted before they connected.
signal participant_joined(participant: NetwParticipant)

## Emitted when this peer's participant has been accepted by the server.
signal local_participant_joined(participant: NetwParticipant)

## Emitted when [member local_participant] changes [member NetwParticipant.current_scene].
signal local_scene_changed(from: MultiplayerScene, to: MultiplayerScene)

## Emitted after the host's startup scenes have been spawned and the server
## is ready to accept the local player. Only relevant for listen server hosts.
signal host_ready()

## Emitted when the connection state changes.
signal state_changed(old_state: NetwSessionInterface.State, new_state: NetwSessionInterface.State)

const ADOPT_CONNECT_TIMEOUT := 15.0

## The current connection state of this tree, mirrored from
## [member NetwMultiplayer.session].
##
## The session machine lives on [member api]. This property forwards to it so
## the tree's readers and [signal state_changed] subscribers keep working while
## the machine reacts to peer assignment.
var state: NetwSessionInterface.State:
	get:
		return api.session.state if api else NetwSessionInterface.State.OFFLINE
	set(value):
		if api:
			api.session.state = value

## The current role of this tree in the session, mirrored from
## [member NetwMultiplayer.session].
var role: NetwSessionInterface.Role:
	get:
		return api.session.role if api else NetwSessionInterface.Role.NONE
	set(value):
		if api:
			api.session.role = value

## Returns [code]true[/code] while this tree is acting as a server
## (dedicated or listen server).
var is_host: bool:
	get:
		_warn_if_role_unset()
		return role == NetwSessionInterface.Role.DEDICATED_SERVER or role == NetwSessionInterface.Role.LISTEN_SERVER

## Returns [code]true[/code] while this tree is acting as a local client
## (including listen server hosts, which are also their own client).
var is_local_client: bool:
	get:
		_warn_if_role_unset()
		return role == NetwSessionInterface.Role.CLIENT or role == NetwSessionInterface.Role.LISTEN_SERVER

## Backward compat. Getter maps to [member is_host]; setter maps to
## [member desired_role].
var is_server: bool:
	get:
		return is_host
	set(value):
		desired_role = NetwSessionInterface.Role.DEDICATED_SERVER if value else NetwSessionInterface.Role.CLIENT


func _warn_if_role_unset() -> void:
	if role == NetwSessionInterface.Role.NONE:
		Netw.dbg.warn(
			"Accessed role-dependent property before role is set. "
			+ "Connect to 'session_entered' before reading is_host/is_local_client.",
		)

## Active transport scheme, such as [code]&"enet"[/code], [code]&"ws"[/code], etc.
@export var scheme: StringName = &"enet":
	set(value):
		scheme = value
		_register_session_config()

## Scheme-specific configuration parameters.
@export var params: Dictionary = {}:
	set(value):
		params = value
		_register_session_config()

## Optional link conditions configuration to simulate latency/loss on this tree.
@export var link_conditions: NetwLinkConditions:
	set(value):
		link_conditions = value
		if api:
			api.connect.connector().link_conditions = value

## On headless builds, automatically starts [method host].
@export var auto_host_headless: bool = true

## The [enum NetwSessionInterface.Role] this tree intends to play once a session starts.
##
## This member is configured intent. The live [member role] is only assigned
## after a connect method succeeds. [constant NetwSessionInterface.Role.NONE] defers the choice to
## whichever method is called.
## [codeblock]
## LISTEN_SERVER
##     host_player()
##     -> host on this tree
##     -> submit the local player
## CLIENT
##     host_player(), join_or_host()
##     -> host a Server sibling
##     -> join that sibling
## DEDICATED_SERVER
##     host()
##     -> host only
## NONE
##     host() -> dedicated
##     join() -> client
##     host_player() -> listen
## [/codeblock]
@export var desired_role: NetwSessionInterface.Role = NetwSessionInterface.Role.LISTEN_SERVER:
	set(value):
		desired_role = value
		update_configuration_warnings()
		_register_session_config()

## Outcome of the last connection handshake.
var last_connect_result: NetwConnectResult = null

## Game-build tag that gates session admission, baked into every build.
##
## A joining peer whose tag differs is rejected during the auth handshake before
## it enters [method MultiplayerAPI.get_peers], so an incompatible build never
## corrupts the session. Bump it whenever the wire protocol breaks. Leave it
## empty to disable the gate.
## [codeblock]
## "" -> tag 0 -> any same version peer admitted (gate off)
## "bomber-v2" -> only peers carrying "bomber-v2" admitted
## [/codeblock]
@export var app_id: StringName = "":
	set(value):
		app_id = value
		_register_session_config()

@export_tool_button("Generate app id") var _generate_app_id := func() -> void:
	app_id = _random_app_id()

@export_group("Debug")
## Auto connect config applied on play in debug builds only.
##
## When set, the tree builds a [JoinPayload] from it and runs
## [method host_player] on ready, skipping [ConnectBrowser]. Release builds
## strip this path because [method OS.has_feature] returns [code]false[/code]
## for [code]"debug"[/code], so the lag settings on [member backend] also apply
## for free during testing. Author [member DebugJoinConfig.spawn_point] to keep
## the join coherent with the server's join handler.
@export var debug_join: DebugJoinConfig

## Owned [NetwMultiplayer] mounted for this session.
##
## A [MultiplayerTree] whose installed API is not [NetwMultiplayer] is
## unrepresentable: [method _mount_api] is the only place that installs one,
## and it never changes identity for the tree's lifetime. Backends that bring
## their own transport swap [member NetwMultiplayer.inner] instead of
## replacing [member api]; see [method _adopt_api].
var api: NetwMultiplayer

## The canonical connector driving this session, owned by [NetwConnect] on the
## api so a tree-scoped and a root-installed session share the one instance.
var connector: NetwConnector:
	get:
		return api.connect.connector() if api else null

## Deprecated compatibility alias for [member api].
var multiplayer_api: MultiplayerAPI:
	get:
		return api

## The active [MultiplayerPeer] connection.
var multiplayer_peer: MultiplayerPeer:
	get:
		return api.multiplayer_peer if api else null

var _tree_name: String = ""
var _join_aborted: bool = false
var _deletion_finalized: bool = false

## Local player [NetwEntity] for this tree, or [code]null[/code].
##
## The session owns the tracking off the liveness bus. This mirrors
## [member NetwMultiplayer.local_player] and [signal local_player_changed]
## re-emits [signal NetwMultiplayer.local_player_changed].
var local_player: NetwEntity:
	get:
		return api.local_player if api else null

## Emitted when [member local_player] is assigned or cleared.
signal local_player_changed(player: NetwEntity)

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	set(value):
		if local_participant == value:
			return
		local_participant = value
		if api:
			api.scenes.bind_local_participant(local_participant)

## Emitted on every peer when the game is paused via [method NetwMultiplayer.pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via
## [method NetwMultiplayer.unpause].
signal tree_unpaused()
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on the server when a client requests to disconnect.
signal disconnect_requested(peer_id: int, reason: String)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)


## Returns the original name of the tree, even if renamed for embedded use.
func get_tree_name() -> String:
	return _tree_name if not _tree_name.is_empty() else name


## Locates the [MultiplayerTree] registered on [param node]'s
## [NetwMultiplayer].
static func for_node(node: Node) -> MultiplayerTree:
	if node is MultiplayerTree:
		return node
	var api := node.multiplayer as NetwMultiplayer
	return api.root as MultiplayerTree if api else null


## Returns the [member role] of the [MultiplayerTree] associated with
## [param node].
static func get_role_for(node: Node) -> NetwSessionInterface.Role:
	var mt := for_node(node)
	return mt.role if mt else NetwSessionInterface.Role.NONE


## Returns the [MultiplayerTree] represented by [param context].
##
## [MultiplayerTree] instances return themselves. [Node] contexts use
## [method for_node] first, then the ancestor chain.
static func resolve(context: Object) -> MultiplayerTree:
	if context is MultiplayerTree:
		return context

	if context is Node:
		var node := context as Node
		var mt := for_node(node)
		if mt:
			return mt

		var p := node.get_parent()
		while p:
			if p is MultiplayerTree:
				return p
			p = p.get_parent()

	return null


## Registers a [Node] as a service for this session.
##
## The registry lives on [member api]. This forwards to it so descendants that
## call through the tree keep working. See [method NetwMultiplayer.register_service].
func register_service(service: Node, type: Script = null) -> void:
	assert(
		is_ancestor_of(service) or service == self,
		"Service %s must be a descendant of the MultiplayerTree." % service.name,
	)
	if api:
		api.register_service(service, type)


## Unregisters a [Node] from this session's services.
func unregister_service(service: Node, type: Script = null) -> void:
	if api:
		api.unregister_service(service, type)


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return api.get_service(type) if api else null


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
##
## Unlike [method get_service] this returns the whole family, so callers can
## collect every [LobbyDirectory] under the tree without knowing each concrete
## subtype.
func get_services(base: Script) -> Array[Node]:
	return api.get_services(base) if api else [] as Array[Node]


## Scans descendant nodes for one whose type matches [param type].
##
## Unlike [method get_service], this works before descendants call
## [method register_service].
func find_service_node(type: Script) -> Node:
	var type_name := type.get_global_name()
	if not type_name.is_empty():
		var matches := find_children("*", type_name, true)
		if not matches.is_empty():
			return matches[0]
	else:
		for child in find_children("*", "", true):
			if child.get_script() == type:
				return child
	return null


## Forcefully clears all internal states and services to break circular
## references during teardown.
func dispose() -> void:
	if api:
		api.clear_services()
	if local_participant and local_participant.current_scene:
		local_participant.current_scene = null
	local_participant = null
	if api:
		api.clear_roster()


## Returns the [NetwPeerContext] for [param peer_id], creating one on first
## access.
##
## The roster lives on [member api]. This forwards so descendants that reach it
## through the tree keep working. See [method NetwMultiplayer.get_peer_context].
func get_peer_context(peer_id: int) -> NetwPeerContext:
	return api.get_peer_context(peer_id) if api else null


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func has_peer_context(peer_id: int) -> bool:
	return api.has_peer_context(peer_id) if api else false


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code].
func get_participant(peer_id: int) -> NetwParticipant:
	return api.get_participant(peer_id) if api else null


## Returns every accepted participant.
func get_participants() -> Array[NetwParticipant]:
	return api.get_participants() if api else []


func _get_accepted_join(peer_id: int) -> ResolvedJoin:
	return api.get_accepted_join(peer_id) if api else null


## Resolves the [SpawnSlot] for [param spawner_path].
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var slot := SpawnSlot.new()

	if api:
		var scene_name := StringName(spawner_path.get_scene_name())
		var scene: MultiplayerScene = api.scenes.scene(scene_name)
		if is_instance_valid(scene):
			slot._scene = scene
			if scene.has_meta(&"_net_scene_token"):
				slot.token = scene.get_meta(&"_net_scene_token")

	return slot


## Returns active player [NetwEntity]s across every [MultiplayerScene].
func get_all_players() -> Array[NetwEntity]:
	return api.scenes.get_all_players() if api else []


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()

	if scheme.is_empty():
		warnings.append(
			"The transport scheme is empty. Please set a scheme such as 'enet' or 'ws'."
		)

	return warnings


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	_mount_api()
	# The candidate is captured now, from the scene authored or wrapped at
	# construction, never a node dropped in later, then handed to the session.
	# Settling defers so a wrapped app-shell scene's own declaration lands first:
	# its child bootstrap registers a NetwSceneConfig after this _enter_tree, and
	# an explicit declaration always wins over adopting a dropped-in bare level.
	api.offer_bare_level(_find_bare_level())
	api.settle.call_deferred()

	# Two-phase debug registration: create the per-tree probe offline (role
	# NONE, peer 0) so the connect lifecycle is observed from the start. The
	# online upgrade (monitors, wire registration) happens in _finalize_session.
	Netw.dbg.register_tree(self)

	for child in get_children():
		if child is MultiplayerSceneManager:
			return


# Selects one unambiguous direct packed level for automatic adoption.
func _find_bare_level() -> Node:
	var candidates: Array[Node] = []
	var gameplay_candidates: Array[Node] = []
	for child in get_children():
		if child.scene_file_path.is_empty() or _is_scene_infrastructure(child):
			continue
		candidates.append(child)
		if not child.find_children(
			"*",
			"MultiplayerSpawner",
			true,
			false,
		).is_empty():
			gameplay_candidates.append(child)
	if gameplay_candidates.size() == 1:
		return gameplay_candidates[0]
	return candidates[0] if candidates.size() == 1 else null


# Excludes known session services from bare-level discovery.
func _is_scene_infrastructure(node: Node) -> bool:
	return node is HostSceneView \
			or node is ParticipantView \
			or node is MultiplayerClock \
			or node is LagCompensation \
			or node is TPLayerAPI \
			or node is LobbyDirectory


## Returns the session [NakamaSessionService], creating it on first access.
##
## A registered service wins, else a dropped-in node is adopted, else a fresh
## node is created and registered. Both
## [NakamaLobbyDirectory] and [NakamaDatabase] resolve this one node so the relay
## lobby and the save store share a single Nakama account.
func get_nakama_session() -> NakamaSessionService:
	if Engine.is_editor_hint():
		return null
	var registered := get_service(NakamaSessionService) as NakamaSessionService
	if is_instance_valid(registered):
		return registered
	var existing := find_service_node(NakamaSessionService) as NakamaSessionService
	if existing:
		register_service(existing)
		return existing
	var session := NakamaSessionService.new()
	session.name = &"NakamaSession"
	add_child(session)
	register_service(session)
	return session


# Folds the build tag into the 32-bit value the auth handshake compares. An
# empty tag means the gate is off, so it must map to 0.
func _compute_app_tag(value: StringName) -> int:
	if String(value).is_empty():
		return 0
	return String(value).hash() & 0xFFFFFFFF


func _app_tag() -> int:
	return _compute_app_tag(app_id)


# Snapshots the session exports into the typed payload the session machine reads.
# The core dispatches on the resource type rather than this node's class, the C++
# port seam. Re-run whenever a registered export changes so live edits propagate.
func _build_session_config() -> NetwSessionConfig:
	var config := NetwSessionConfig.new()
	config.app_id = app_id
	config.desired_role = desired_role
	return config


func _register_session_config() -> void:
	if api:
		api.object_configuration_add(self, _build_session_config())


# Builds a fresh random build tag for the editor "Generate app id" button.
func _random_app_id() -> StringName:
	const CHARS := "abcdefghijklmnopqrstuvwxyz0123456789"
	var out := ""
	for i in 15:
		out += CHARS[randi() % CHARS.length()]
	return StringName(out)


func _init() -> void:
	if not Engine.is_editor_hint():
		api = _make_api()
		tree_exiting.connect(_on_exiting)


# The one construction point for the branch API (a MultiplayerTree whose API
# is not NetwMultiplayer is unrepresentable). Test rigs override this to
# install a capturing NetwMultiplayer subclass.
func _make_api() -> NetwMultiplayer:
	return NetwMultiplayer.new(SceneMultiplayer.new())


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return

	if connector:
		connector.poll(dt)
	if api and api.has_multiplayer_peer():
		api.poll()
	if api:
		api.persistence.tick(dt)


func _build_host_config() -> NetwHostConfig:
	var config := NetwHostConfig.new()
	config.scheme = scheme
	config.params = params.duplicate()
	if "max_clients" in params:
		config.max_players = int(params.max_clients)
	elif "max_players" in params:
		config.max_players = int(params.max_players)
	return config


# Starts this tree as a server over scheme with params, driving
# NetwConnector.host and setting role. The public host() wraps this to also
# submit a local JoinPayload; a dedicated server hosts through this directly.
func _open_host(quiet: bool = false, options: LobbyDirectory.HostOptions = null) -> Error:
	assert(state == NetwSessionInterface.State.OFFLINE, "Must be offline to host.")
	if scheme.is_empty():
		if not quiet:
			Netw.dbg.error(
				"MultiplayerTree.host: no transport scheme configured.",
				[],
				func(m): push_error(m)
			)
		return ERR_UNCONFIGURED

	Netw.dbg.trace("MultiplayerTree: Hosting session.")
	_transition(NetwSessionInterface.State.CONNECTING)

	var config := _build_host_config()
	if options:
		config.server_name = options.server_name
		config.visibility = options.visibility

	var attempt := connector.host(config, null)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if not res.is_ok():
		_transition(NetwSessionInterface.State.OFFLINE)
		last_connect_result = NetwConnectResult.error(res.message)
		if not quiet:
			Netw.dbg.error("Failed to host: %s", [res.message])
		return ERR_CANT_CREATE

	role = NetwSessionInterface.Role.LISTEN_SERVER if desired_role == NetwSessionInterface.Role.LISTEN_SERVER \
	else NetwSessionInterface.Role.DEDICATED_SERVER
	_transition(NetwSessionInterface.State.ONLINE)
	return OK


## Connects to [param target] and submits [param join_payload].
##
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "valeria"
##
## var target := NetwConnectTarget.new()
## target.scheme = &"ws"
## target.address = "127.0.0.1"
##
## var err := await tree.join(target, payload)
## [/codeblock]
func join(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
		_timeout: float = 5.0,
		quiet: bool = false,
) -> Error:
	last_connect_result = null
	assert(state == NetwSessionInterface.State.OFFLINE, "Must be offline to join.")

	assert(
		desired_role != NetwSessionInterface.Role.DEDICATED_SERVER,
		"join() needs a local player; a dedicated server hosts via host().",
	)
	_join_aborted = false
	if target == null:
		Netw.dbg.error("join: target is null.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER

	var attempt := connector.join(target, join_payload)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res.is_ok():
		last_connect_result = res
		role = NetwSessionInterface.Role.CLIENT
		_transition(NetwSessionInterface.State.ONLINE)
		return OK
	else:
		_transition(NetwSessionInterface.State.OFFLINE)
		if res.status == NetwConnectResult.Status.ABORTED or _join_aborted:
			last_connect_result = NetwConnectResult.aborted(res.message)
		elif res.status == NetwConnectResult.Status.TIMED_OUT:
			last_connect_result = NetwConnectResult.timed_out(res.message)
		else:
			last_connect_result = NetwConnectResult.error(res.message)

		if not quiet:
			Netw.dbg.error("Failed to join: %s", [res.message])
		return ERR_CANT_CONNECT


## Joins [param target], or hosts when no listener replies.
##
## [codeblock]
## var err := await tree.join_or_host(target, payload)
## if err == OK and tree.is_host:
##     print("Hosting")
## [/codeblock]
func join_or_host(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
) -> Error:
	assert(state == NetwSessionInterface.State.OFFLINE, "Must be offline to connect.")
	assert(
		desired_role != NetwSessionInterface.Role.DEDICATED_SERVER,
		"join_or_host() needs a local player; a dedicated server hosts via host().",
	)
	if target == null:
		Netw.dbg.error("join_or_host: target is null.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER

	var config := _build_host_config()

	var attempt := connector.join_or_host(target, config, join_payload)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res.is_ok():
		last_connect_result = NetwConnectResult.ok()
		return OK
	else:
		return ERR_CANT_CONNECT


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	return (api != null
			and api.has_multiplayer_peer()
			and not api.multiplayer_peer is OfflineMultiplayerPeer
			and api.multiplayer_peer.get_connection_status()
			== MultiplayerPeer.CONNECTION_CONNECTED)


## Aborts [constant NetwSessionInterface.State.CONNECTING] and returns to
## [constant NetwSessionInterface.State.OFFLINE].
func abort_join() -> void:
	if state != NetwSessionInterface.State.CONNECTING:
		return
	_join_aborted = true
	Netw.dbg.info("MultiplayerTree: aborting connection handshake.")
	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()
		api.multiplayer_peer = OfflineMultiplayerPeer.new()
	_transition(NetwSessionInterface.State.OFFLINE)


## Flushes local save state and closes [member multiplayer_peer].
##
## [member state] returns to [constant NetwSessionInterface.State.OFFLINE] before this method
## completes. [signal session_ended] fires on the way out so session-scoped
## subscribers tear down.
func leave() -> void:
	if api:
		await api.session.leave()


## Pauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	api.session.pause(reason)


## Unpauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	api.session.unpause()


## Disconnects [param peer_id] from the session.
##
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	api.session.kick(peer_id, reason)


## Asks the server to kick [param peer_id].
##
## Forwards to [method NetwSessionInterface.request_kick], which rides
## [constant NetwFrameEnvelope.Channel.SESSION_KICK_REQUEST].
##
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	if api:
		api.session.request_kick(peer_id, reason)


## Asks the server for permission to leave.
##
## Forwards to [method NetwSessionInterface.request_leave], which rides
## [constant NetwFrameEnvelope.Channel.SESSION_LEAVE_REQUEST].
##
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	if api:
		api.session.request_leave(reason)


## Notifies all clients that the server is shutting down.
##
## Forwards to [method NetwSessionInterface.notify_shutdown], which broadcasts the
## notice over [constant NetwFrameEnvelope.Channel.SESSION_SHUTDOWN].
##
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	if api:
		api.session.notify_shutdown(reason)


## Hosts a session and submits the local [param join_payload].
##
## Use [method join_or_host] when the caller should probe before hosting.
## [method host] is the direct host path.
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "Host"
##
## var err := await tree.host(payload)
## [/codeblock]
func host(join_payload: JoinPayload, options: LobbyDirectory.HostOptions = null) -> Error:
	assert(state == NetwSessionInterface.State.OFFLINE, "Must be offline to host.")
	assert(
		desired_role != NetwSessionInterface.Role.DEDICATED_SERVER,
		"host() needs a local player; a dedicated server hosts via _open_host().",
	)
	var err := await _prepare_session(join_payload)
	if err != OK:
		return err

	return await _host_player_logic(join_payload, options)


func _prepare_session(join_payload: JoinPayload) -> Error:
	var prepare_err := await api.session.prepare_join(join_payload)
	if prepare_err != OK:
		return prepare_err
	await leave()
	return OK


func _host_player_logic(
		join_payload: JoinPayload,
		options: LobbyDirectory.HostOptions = null,
) -> Error:
	# LISTEN_SERVER and NONE host on this tree; CLIENT spins up an embedded
	# dedicated sibling and joins it.
	if desired_role != NetwSessionInterface.Role.CLIENT:
		var host_err := await _open_host(true, options)

		if host_err == OK:
			role = NetwSessionInterface.Role.LISTEN_SERVER
			if get_service(MultiplayerSceneManager):
				await _await_host_scenes()
			submit_join(join_payload)
			return OK
		elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
			var prepare_err := await api.session.prepare_join(join_payload)
			if prepare_err != OK:
				return prepare_err
			return await _join_local_host(join_payload)
		else:
			return host_err

	var server := duplicate() as MultiplayerTree
	server.desired_role = NetwSessionInterface.Role.DEDICATED_SERVER
	server.name = "Server"
	server.auto_host_headless = false
	get_parent().add_child.call_deferred(server)
	await get_tree().process_frame

	var client_sm := get_service(MultiplayerSceneManager)
	if client_sm:
		var server_sm := server.get_service(MultiplayerSceneManager)
		for path in client_sm.get_configured_paths():
			server_sm._configure_default(path)

	var host_err := await server._open_host(true)
	if host_err == OK:
		return await _join_local_host(join_payload, _loopback_join_address(server))
	elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
		server.queue_free.call_deferred()
		return await _join_local_host(join_payload)
	else:
		server.queue_free.call_deferred()
		return host_err


# Waits until the host's startup scenes exist so the local join spawns into a
# live scene. Returns at once when they already spawned (the [signal host_ready]
# relay can fire before this awaits), otherwise waits for the relay with a frame
# cap so a missed one-shot signal cannot hang the host.
func _await_host_scenes() -> void:
	if not api.scenes.scenes.is_empty():
		return
	var fired := [false]
	var cb := func() -> void: fired[0] = true
	host_ready.connect(cb, CONNECT_ONE_SHOT)
	var guard := 0
	while not fired[0] and api.scenes.scenes.is_empty() and guard < 600:
		await get_tree().process_frame
		guard += 1
	if host_ready.is_connected(cb):
		host_ready.disconnect(cb)


# Reads the loopback address a same-machine host is listening on from its
# resolved NetwPeerView, falling back to localhost when none is surfaced.
func _loopback_join_address(tree: MultiplayerTree = self) -> String:
	var view := tree.connector.peer_view if tree.connector else null
	if view:
		var addr: String = view.join_address()
		if not addr.is_empty():
			return addr
	return "localhost"


# Joins a same-machine host over this tree's transport scheme, used by the
# host-with-local-player fallbacks and the embedded-server path.
func _join_local_host(join_payload: JoinPayload, address: String = "") -> Error:
	if address.is_empty():
		address = _loopback_join_address()
	var target := NetwConnectTarget.new()
	target.scheme = scheme
	target.address = address
	return await join(target, join_payload)


## Submits [param join_payload] to the server through the session machine's
## carrier join frame. See [method NetwSessionInterface.submit_join].
func submit_join(join_payload: JoinPayload) -> void:
	if api:
		api.session.submit_join(join_payload)


# Waits for an adopted client peer to finish Godot's connection handshake.
func _await_adopted_client_connected() -> Error:
	if not api or not api.has_multiplayer_peer():
		return ERR_UNCONFIGURED
	if api.multiplayer_peer.get_connection_status() \
			== MultiplayerPeer.CONNECTION_CONNECTED:
		return OK

	var timer := get_tree().create_timer(ADOPT_CONNECT_TIMEOUT)
	if await Async.timeout(connected_to_server, timer):
		Netw.dbg.error(
			"adopt_peer: timed out waiting for the adopted peer to connect.",
			func(m): push_error(m)
		)
		return ERR_TIMEOUT
	return OK


func _ready() -> void:
	if Engine.is_editor_hint():
		return

	if Netw.is_test_env():
		return

	if auto_host_headless and not scheme.is_empty() \
			and DisplayServer.get_name() == "headless":
		# host() resolves the live role from desired_role.
		if desired_role == NetwSessionInterface.Role.LISTEN_SERVER \
				or desired_role == NetwSessionInterface.Role.DEDICATED_SERVER:
			await _open_host()
		return

	if debug_join != null and not scheme.is_empty() \
			and desired_role != NetwSessionInterface.Role.DEDICATED_SERVER \
			and OS.has_feature("debug"):
		await _debug_autoconnect()


# Restores the old init_payload_debug flow: host straight into the game from
# the editor, no ConnectBrowser. Debug-only, so release never auto-connects.
func _debug_autoconnect() -> void:
	if state != NetwSessionInterface.State.OFFLINE:
		return
	var payload := debug_join.to_payload()
	Netw.dbg.info(
		"MultiplayerTree: debug auto-connect as '%s'.",
		[payload.username],
	)
	await host(payload)


# The server admission policy the session machine calls for each join. Resolves
# identity, rejects an invalid payload or a losing username collision, and
# returns the admitted join. Wired into NetwSessionInterface.join_gate so a
# tree-free session with no gate falls back to open resolve.
func _server_join_gate(join_payload: JoinPayload, peer_id: int) -> ResolvedJoin:
	var rj := join_payload.resolve()
	if not rj:
		Netw.dbg.warn("join: invalid payload from peer %d", [peer_id])
		return null
	if not _resolve_username_collision(rj):
		return null
	return rj


# Emits join signals derived from the accepted server authority data.
func _emit_participant_joined(participant: NetwParticipant) -> void:
	# Bind local_participant first so its scene_changed relay is connected before
	# any participant_joined handler admits the local player. On a listen server
	# admission is synchronous, so a late bind would drop the first
	# local_scene_changed.
	var is_local := participant.peer_id == multiplayer.get_unique_id()
	if is_local:
		local_participant = participant

	participant_joined.emit(participant)

	if is_local:
		local_participant_joined.emit(participant)


# Reacts to the session machine admitting a participant, running the tree's own
# join side effects (local-player binding, spawn) after the roster is remembered.
func _on_participant_admitted(peer_id: int) -> void:
	var participant := get_participant(peer_id)
	if participant:
		_emit_participant_joined(participant)


# Returns true when the join should proceed.
func _resolve_username_collision(rj: ResolvedJoin) -> bool:
	if not api:
		return false
	return api._roster.resolve_username_collision(
		rj,
		get_all_players(),
		api.inner.disconnect_peer,
	)


# Mounts the owned api onto the SceneTree path and binds signals.
func _mount_api() -> void:
	if not api:
		return

	_tree_name = name
	var root_path := get_path()
	api.inner.root_path = root_path
	get_tree().set_multiplayer(api, root_path)
	assert(
		get_tree().get_multiplayer(root_path) == api,
		"MultiplayerTree: _mount_api must be the only installation point on this branch.",
	)
	_bind_api_signals(api)
	_register_session_config()


# Clears the custom multiplayer API from the SceneTree path.
func _unmount_api() -> void:
	if not api:
		return

	_unbind_api_signals(api)

	if not api.inner.root_path.is_empty():
		get_tree().set_multiplayer(null, api.inner.root_path)


# Swaps the wrapped [SceneMultiplayer] for backends that bring their own peer
# transport. [member api] itself, its installation on the tree's branch, and
# every cached [code]NetwMultiplayer.of(node)[/code] reference stay valid
# across the swap. Only [member NetwMultiplayer.inner] changes.
func _adopt_api(new_inner: SceneMultiplayer, reason: String) -> void:
	if not api or new_inner == api.inner:
		return

	Netw.dbg.trace(
		"MultiplayerTree: Adopting backend-provided SceneMultiplayer (%s).",
		[reason],
	)
	api.adopt_inner(new_inner)
	api.inner.root_path = get_path()


# Forwards a state change to the session machine on [member api], which owns the
# legal-edge table and the enter and exit hooks. The tree runs its own
# session-scoped finalize and teardown off the machine's session_entered and
# session_ended signals instead.
func _transition(next: NetwSessionInterface.State) -> void:
	if api:
		api.session.transition(next)


# Finalizes the session once the peer is live and the role is set. Rides
# [signal NetwSessionInterface.session_entered].
func _finalize_session() -> void:
	Netw.dbg.trace("MultiplayerTree: Finalizing session.")
	Netw.dbg.debug(
		"MultiplayerTree: session app_id='%s' app_tag=0x%08x.",
		[String(app_id), _app_tag()],
	)
	Netw.dbg.finalize_tree(self)
	session_entered.emit()

	if get_service(MultiplayerSceneManager) \
			and not api.scenes.startup_scenes_spawned.is_connected(host_ready.emit):
		api.scenes.startup_scenes_spawned.connect(host_ready.emit)


# Mirror of [method _finalize_session]. Releases the session so session-scoped
# subscribers unwind and a same-tree re-host starts clean. The embedded Server
# sibling spun up by _host_player_logic is freed here too.
func _teardown_session() -> void:
	if local_participant and local_participant.current_scene:
		local_participant.current_scene = null
	local_participant = null
	session_ended.emit()
	if api:
		api.clear_roster()
	role = NetwSessionInterface.Role.NONE

	var parent := get_parent()
	if parent:
		var server := parent.get_node_or_null("Server") as MultiplayerTree
		if server and server != self:
			server.queue_free.call_deferred()


func _bind_api_signals(target: NetwMultiplayer) -> void:
	if not target:
		return
	# The tree authors link conditions on the session's one shared connector.
	if link_conditions:
		target.connect.connector().link_conditions = link_conditions
	# The session owns local_player tracking off the liveness bus. The tree only
	# re-emits its edge so consumers bound to tree.local_player_changed keep
	# their tree-facing signal.
	if not target.local_player_changed.is_connected(local_player_changed.emit):
		target.local_player_changed.connect(local_player_changed.emit)
	if not target.peer_connected.is_connected(_on_peer_connected):
		target.peer_connected.connect(_on_peer_connected)
	if not target.peer_disconnected.is_connected(_on_peer_disconnected):
		target.peer_disconnected.connect(_on_peer_disconnected)
	if not target.connected_to_server.is_connected(_on_connected_to_server):
		target.connected_to_server.connect(_on_connected_to_server)
	if not target.server_disconnected.is_connected(_on_server_disconnected):
		target.server_disconnected.connect(_on_server_disconnected)
	# The session shutdown handler owns the graceful-disconnect notice, and the
	# session request handlers own the kick/leave requests. The tree re-emits their
	# edges so consumers bound to the tree-facing signals keep them.
	if not target.server_disconnecting.is_connected(server_disconnecting.emit):
		target.server_disconnecting.connect(server_disconnecting.emit)
	if not target.kick_requested.is_connected(kick_requested.emit):
		target.kick_requested.connect(kick_requested.emit)
	if not target.disconnect_requested.is_connected(disconnect_requested.emit):
		target.disconnect_requested.connect(disconnect_requested.emit)
	if not target.tree_paused.is_connected(tree_paused.emit):
		target.tree_paused.connect(tree_paused.emit)
	if not target.tree_unpaused.is_connected(tree_unpaused.emit):
		target.tree_unpaused.connect(tree_unpaused.emit)
	if not target.kicked.is_connected(kicked.emit):
		target.kicked.connect(kicked.emit)
	if not target.scenes.local_scene_changed.is_connected(
		local_scene_changed.emit,
	):
		target.scenes.local_scene_changed.connect(local_scene_changed.emit)
	# The session machine on the api owns state, role, and the lifecycle edges.
	# The tree re-emits its edge and runs its own session-scoped finalize and
	# teardown off the machine's signals.
	if not target.session.state_changed.is_connected(_on_session_state_changed):
		target.session.state_changed.connect(_on_session_state_changed)
	if not target.session.session_entered.is_connected(_finalize_session):
		target.session.session_entered.connect(_finalize_session)
	if not target.session.session_ended.is_connected(_teardown_session):
		target.session.session_ended.connect(_teardown_session)
	# The tree supplies the server admission policy and runs its join side
	# effects off the machine's admission signal.
	target.session.join_gate = _server_join_gate
	if not target.session.participant_admitted.is_connected(_on_participant_admitted):
		target.session.participant_admitted.connect(_on_participant_admitted)


func _unbind_api_signals(target: NetwMultiplayer) -> void:
	if not target:
		return
	target.session.join_gate = Callable()
	if target.session.participant_admitted.is_connected(_on_participant_admitted):
		target.session.participant_admitted.disconnect(_on_participant_admitted)
	if target.session.state_changed.is_connected(_on_session_state_changed):
		target.session.state_changed.disconnect(_on_session_state_changed)
	if target.session.session_entered.is_connected(_finalize_session):
		target.session.session_entered.disconnect(_finalize_session)
	if target.session.session_ended.is_connected(_teardown_session):
		target.session.session_ended.disconnect(_teardown_session)
	if target.local_player_changed.is_connected(local_player_changed.emit):
		target.local_player_changed.disconnect(local_player_changed.emit)
	if target.server_disconnecting.is_connected(server_disconnecting.emit):
		target.server_disconnecting.disconnect(server_disconnecting.emit)
	if target.kick_requested.is_connected(kick_requested.emit):
		target.kick_requested.disconnect(kick_requested.emit)
	if target.disconnect_requested.is_connected(disconnect_requested.emit):
		target.disconnect_requested.disconnect(disconnect_requested.emit)
	if target.peer_connected.is_connected(_on_peer_connected):
		target.peer_connected.disconnect(_on_peer_connected)
	if target.peer_disconnected.is_connected(_on_peer_disconnected):
		target.peer_disconnected.disconnect(_on_peer_disconnected)
	if target.connected_to_server.is_connected(_on_connected_to_server):
		target.connected_to_server.disconnect(_on_connected_to_server)
	if target.server_disconnected.is_connected(_on_server_disconnected):
		target.server_disconnected.disconnect(_on_server_disconnected)
	if target.tree_paused.is_connected(tree_paused.emit):
		target.tree_paused.disconnect(tree_paused.emit)
	if target.tree_unpaused.is_connected(tree_unpaused.emit):
		target.tree_unpaused.disconnect(tree_unpaused.emit)
	if target.kicked.is_connected(kicked.emit):
		target.kicked.disconnect(kicked.emit)
	if target.scenes.local_scene_changed.is_connected(local_scene_changed.emit):
		target.scenes.local_scene_changed.disconnect(local_scene_changed.emit)


# Re-emits the session machine's state edge on the tree so [signal state_changed]
# subscribers keep their tree-facing signal.
func _on_session_state_changed(old_state: NetwSessionInterface.State, new_state: NetwSessionInterface.State) -> void:
	state_changed.emit(old_state, new_state)


func _notification(what: int) -> void:
	# A tree freed through a parent (rather than its own queue_free) reaches
	# tree_exiting with is_queued_for_deletion() false, so _on_exiting treats it
	# as a reparent and leaves the peer mounted. PREDELETE is the unambiguous
	# deletion signal (it never fires on reparent), so release the peer here when
	# the queued tree_exiting path did not already run.
	if what == NOTIFICATION_PREDELETE:
		_close_peer_on_delete()
		if api:
			api.dispose()
	elif what == NOTIFICATION_WM_CLOSE_REQUEST and not Engine.is_editor_hint():
		# The server saves every persisted entity before quitting. Clients accept
		# the close immediately, so only a host that armed the guard defers here.
		if api:
			api.persistence.handle_shutdown()


func _on_exiting() -> void:
	Netw.dbg.trace("MultiplayerTree: Exiting.")

	# When re-parenting, we only unmount the api from the previous path to
	# keep the connection alive. _enter_tree handles re-registration.
	if not is_queued_for_deletion():
		_unmount_api()
		return

	Netw.dbg.unregister_tree(self)
	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()
		api.multiplayer_peer = null
	_unmount_api()

	dispose()
	_deletion_finalized = true


# Releases the live peer and breaks circular references for a tree freed via a
# parent, the case _on_exiting misreads as a reparent. Idempotent through
# [member _deletion_finalized] so it never double-tears-down with _on_exiting.
func _close_peer_on_delete() -> void:
	if _deletion_finalized or Engine.is_editor_hint():
		return
	# A node still in the tree at PREDELETE is part of a SceneTree-wide teardown
	# cascade, where closing the peer makes siblings error on get_unique_id and
	# the leak no longer matters. Only the genuine parent-freed case (already
	# detached by tree_exiting) needs cleanup here.
	if is_inside_tree():
		return
	_deletion_finalized = true

	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()
		api.multiplayer_peer = null

	dispose()


func _on_peer_connected(peer_id: int) -> void:
	Netw.dbg.info("Peer connected: %d", [peer_id])
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	Netw.dbg.info("Peer disconnected: %d", [peer_id])
	if api:
		var participant := api.get_participant(peer_id)
		if participant and participant.current_scene:
			participant.current_scene = null
		api.forget_peer(peer_id)
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	Netw.dbg.info("Connected to server as peer %d.", [peer_id])

	set_multiplayer_authority(peer_id, false)
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	Netw.dbg.info("Disconnected from server.")
	# The session machine on the api owns the crash teardown edge. The tree only
	# re-emits its own public signal for session-scoped subscribers.
	server_disconnected.emit()
