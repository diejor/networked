@icon("res://addons/networked/assets/MultiplayerTree.svg")
@tool
class_name MultiplayerTree
extends Node
## Root node for one Networked session.
##
## [member api], [member backend], [member desired_role], [member role],
## [member state], [member spawn_policy], and the session service registry all
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
## var target := JoinTarget.new()
## target.backend = ENetBackend.new()
## target.address = "127.0.0.1"
## var join_err := await tree.join(target, payload)
## [/codeblock]

## Emitted when [member api], [member role], and session services are ready.
##
## Fires on entering [constant State.ONLINE], so it pairs with
## [signal session_ended] and the two strictly alternate. A connect attempt that
## fails before [constant State.ONLINE] emits neither, so a subscriber that wires
## session state here can rely on exactly one matching [signal session_ended].
signal session_entered()

## Emitted when the tree leaves [constant State.ONLINE] and tears down the
## session.
##
## Fires on exiting [constant State.ONLINE] through either
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
signal local_scene_changed(from: NetwScene, to: NetwScene)

## Emitted after the host's startup scenes have been spawned and the server
## is ready to accept the local player. Only relevant for listen server hosts.
signal host_ready()

## Emitted when the connection state changes.
signal state_changed(old_state: State, new_state: State)

## Emitted after [member backend] is cloned for a client join attempt.
signal backend_ready_for_join(backend: BackendPeer)

# Internal signal to relay connection failure outcomes.
signal _connect_failed(result: BackendPeer.ConnectResult)

## Session lifecycle state for this tree.
## [codeblock]
## OFFLINE
##   | _open_host() / join() / host()
##   v
## CONNECTING --(failure or abort_join)--> OFFLINE
##   | success
##   v
## ONLINE
##   | leave()
##   v
## DISCONNECTING --> OFFLINE
## [/codeblock]
enum State {
	## No active [MultiplayerPeer] is mounted.
	OFFLINE,
	## A host, join, or adopt operation is configuring transport.
	CONNECTING,
	## The tree has an active [MultiplayerPeer] and configured services.
	ONLINE,
	## The tree is closing the active peer and clearing session state.
	DISCONNECTING,
}

## Runtime role this tree plays in the current session.
enum Role {
	## No session role has been assigned yet.
	NONE,
	## This tree is connected to a remote server as a client.
	CLIENT,
	## This tree hosts the session without acting as a local client.
	DEDICATED_SERVER,
	## This tree hosts the session and also represents the local player.
	LISTEN_SERVER,
}

const ADOPT_CONNECT_TIMEOUT := 15.0

# Every legal [enum State] edge keyed by source state. [method _transition]
# hard-asserts against this so every entry and exit path converges on the same
# transitions instead of one per caller. The server-crash route reuses
# ONLINE -> DISCONNECTING -> OFFLINE rather than a direct ONLINE -> OFFLINE edge.
const _LEGAL_EDGES := {
	State.OFFLINE: [State.CONNECTING],
	State.CONNECTING: [State.ONLINE, State.OFFLINE],
	State.ONLINE: [State.DISCONNECTING],
	State.DISCONNECTING: [State.OFFLINE],
}

## The current connection state of this tree.
##
## This var only stores the value and emits [signal state_changed]. Drive it
## through [method _transition] so setup and teardown hooks run on each edge.
var state: State = State.OFFLINE:
	set(new_state):
		if state == new_state:
			return
		var old := state
		state = new_state
		state_changed.emit(old, new_state)

## The current role of this tree in the session.
var role: Role = Role.NONE

## Returns [code]true[/code] while this tree is acting as a server
## (dedicated or listen server).
var is_host: bool:
	get:
		_warn_if_role_unset()
		return role == Role.DEDICATED_SERVER or role == Role.LISTEN_SERVER

## Returns [code]true[/code] while this tree is acting as a local client
## (including listen server hosts, which are also their own client).
var is_local_client: bool:
	get:
		_warn_if_role_unset()
		return role == Role.CLIENT or role == Role.LISTEN_SERVER

## Backward compat. Getter maps to [member is_host]; setter maps to
## [member desired_role].
var is_server: bool:
	get:
		return is_host
	set(value):
		desired_role = Role.DEDICATED_SERVER if value else Role.CLIENT


func _warn_if_role_unset() -> void:
	if role == Role.NONE:
		Netw.dbg.warn(
			"Accessed role-dependent property before role is set. "
			+ "Connect to 'session_entered' before reading is_host/is_local_client.",
		)

## Default and active [BackendPeer] transport for this tree.
##
## [method host] uses this value directly. [method join] and
## [method join_or_host] replace it with an instance made from
## [member JoinTarget.backend]. Assignment duplicates the resource so each live
## session owns its transport state.
@export var backend: BackendPeer:
	set(value):
		if not Engine.is_editor_hint():
			if value:
				backend = value.clone()
			else:
				backend = null
		else:
			if backend and backend.changed.is_connected(
				update_configuration_warnings,
			):
				backend.changed.disconnect(update_configuration_warnings)

			backend = value

			if backend and not backend.changed.is_connected(
				update_configuration_warnings,
			):
				backend.changed.connect(update_configuration_warnings)

		update_configuration_warnings()

## On headless builds, automatically starts [method host].
@export var auto_host_headless: bool = true

## The [enum Role] this tree intends to play once a session starts.
##
## This member is configured intent. The live [member role] is only assigned
## after a connect method succeeds. [constant Role.NONE] defers the choice to
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
@export var desired_role: Role = Role.LISTEN_SERVER:
	set(value):
		desired_role = value
		update_configuration_warnings()

## Optional [NetwAuth] for [method join] and [method join_or_host].
##
## A [code]null[/code] provider skips authentication. The server trusts the
## client supplied [member JoinPayload.username].
@export var auth_provider: NetwAuth:
	set(value):
		auth_provider = value
		if _auth:
			_auth.set_auth_provider(value)
			_auth.prepare()

## Outcome of the last connection handshake.
var last_connect_result: BackendPeer.ConnectResult = null

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
		if _auth:
			_auth.set_app_tag(_compute_app_tag(value))

@export_tool_button("Generate app id") var _generate_app_id := func() -> void:
	app_id = _random_app_id()

## Builds [ServerDescriptor.Info] for [method BackendPeer.probe_server_info]. When this
## member is [code]null[/code], [AuthProtocol.Responder] creates a
## [DefaultServerDescriptor] while answering a probe.
##
## [DefaultServerDescriptor] derives live values from this tree.
## [codeblock]
##     func build_server_info(tree: MultiplayerTree) -> ServerDescriptor.Info:
##         var info := ServerDescriptor.Info.new()
##         info.players = tree.get_participants().size()
##         info.app_id = tree.app_id
##         return info
## [/codeblock]
## Read [ServerDescriptor] a`nd [DefaultServerDescriptor] before assigning a
## custom source.
@export var server_info_source: ServerDescriptor:
	set(value):
		server_info_source = value
		if _auth:
			_auth.set_server_info_source(value)

## Server side [SpawnPolicy] for accepted joins.
##
## A [code]null[/code] value means [signal participant_joined] is the gameplay
## entry point.
## [codeblock]
## # Client. Store spawn intent in JoinPayload.spawn.
## payload.spawn = spawn_policy.to_dict()
##
## # Server. MultiplayerTree calls spawn after accepting the join.
## var scene := await spawn_policy.spawn(rj, Netw.of(tree))
## [/codeblock]
## [method SpawnPolicy.to_dict] serializes client intent.
## [method SpawnPolicy.spawn] reads [member ResolvedJoin.spawn] and returns
## the [MultiplayerScene] that receives the player. Read [SpawnPolicy] and
## [EntitySpawnPolicy] before assigning a custom policy.
@export var spawn_policy: SpawnPolicy

@export_group("Debug")
## Auto connect config applied on play in debug builds only.
##
## When set, the tree builds a [JoinPayload] from it and runs
## [method host_player] on ready, skipping [ConnectBrowser]. Release builds
## strip this path because [method OS.has_feature] returns [code]false[/code]
## for [code]"debug"[/code], so the lag settings on [member backend] also apply
## for free during testing. Author [member DebugJoinConfig.spawn] as the same
## class as [member spawn_policy] to keep the join coherent.
@export var debug_join: DebugJoinConfig

## Owned [NetwMultiplayer] mounted for this session.
##
## A [MultiplayerTree] whose installed API is not [NetwMultiplayer] is
## unrepresentable: [method _mount_api] is the only place that installs one,
## and it never changes identity for the tree's lifetime. Backends that bring
## their own transport swap [member NetwMultiplayer.inner] instead of
## replacing [member api]; see [method _adopt_api].
var api: NetwMultiplayer

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
## [signal local_player_changed] fires whenever this member changes.
var local_player: NetwEntity:
	set(value):
		if local_player != value:
			local_player = value
			local_player_changed.emit(value)

## Emitted when [member local_player] is assigned or cleared.
signal local_player_changed(player: NetwEntity)

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	set(value):
		if local_participant == value:
			return
		if local_participant \
				and local_participant.scene_changed.is_connected(
					_on_local_participant_scene_changed,
				):
			local_participant.scene_changed.disconnect(
				_on_local_participant_scene_changed,
			)
		local_participant = value
		if local_participant:
			local_participant.scene_changed.connect(
				_on_local_participant_scene_changed,
			)
			_sync_local_participant_scene.call_deferred()

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
	return api.tree if api else null


## Returns the [member role] of the [MultiplayerTree] associated with
## [param node].
static func get_role_for(node: Node) -> Role:
	var mt := for_node(node)
	return mt.role if mt else Role.NONE


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


var _roster: SessionRoster = SessionRoster.new()
var _participants: Dictionary[int, NetwParticipant] = { }
var _auth: AuthCoordinator
var _services: ServiceRegistry = ServiceRegistry.new()
var _client_join_payload: JoinPayload
var _interpolation_interface: NetwInterpolationInterface


## Registers a [Node] as a service for this session.
func register_service(service: Node, type: Script = null) -> void:
	assert(
		is_ancestor_of(service) or service == self,
		"Service %s must be a descendant of the MultiplayerTree." % service.name,
	)
	_services.register_service(service, type)


## Unregisters a [Node] from this session's services.
func unregister_service(service: Node, type: Script = null) -> void:
	_services.unregister_service(service, type)


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.get_service(type)


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
##
## Unlike [method get_service] this returns the whole family, so callers can
## collect every [LobbyDirectory] under the tree without knowing each concrete
## subtype.
func get_services(base: Script) -> Array[Node]:
	return _services.get_services(base)


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
	if _auth:
		_auth.clear()
	_services.clear()
	if local_participant and local_participant.current_scene:
		local_participant.current_scene = null
	local_participant = null
	_roster.clear()
	_participants.clear()
	_client_join_payload = null


## Returns the [NetwPeerContext] for [param peer_id], creating one on first
## access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	return _roster.get_peer_context(peer_id)


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func has_peer_context(peer_id: int) -> bool:
	return _roster.has_peer_context(peer_id)


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code].
func get_participant(peer_id: int) -> NetwParticipant:
	if _get_accepted_join(peer_id) == null:
		return null
	if not _participants.has(peer_id):
		_participants[peer_id] = NetwParticipant.new(self, peer_id)
	return _participants[peer_id]


## Returns every accepted participant.
func get_participants() -> Array[NetwParticipant]:
	var result: Array[NetwParticipant] = []
	for rj: ResolvedJoin in _roster.get_accepted_joins():
		var participant := get_participant(rj.peer_id)
		if participant:
			result.append(participant)
	return result


func _get_accepted_join(peer_id: int) -> ResolvedJoin:
	return _roster.get_accepted_join(peer_id)


## Resolves the [SpawnSlot] for [param spawner_path].
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var slot := SpawnSlot.new()
	var sm: MultiplayerSceneManager = get_service(MultiplayerSceneManager)

	if sm:
		var scene_name := StringName(spawner_path.get_scene_name())
		var scene: MultiplayerScene = sm.active_scenes.get(scene_name)
		if is_instance_valid(scene):
			slot._scene = scene
			if scene.has_meta(&"_net_scene_token"):
				slot.token = scene.get_meta(&"_net_scene_token")

	return slot


## Returns active player [NetwEntity]s across every [MultiplayerScene].
func get_all_players() -> Array[NetwEntity]:
	var sm: MultiplayerSceneManager = get_service(MultiplayerSceneManager)
	if sm:
		return sm.get_all_players()
	return []


## Returns the ancestor [MultiplayerScene] containing [param node], or
## [code]null[/code].
static func scene_for_node(node: Node) -> MultiplayerScene:
	var p := node.get_parent()
	while p:
		if p is MultiplayerScene:
			return p as MultiplayerScene
		p = p.get_parent()
	return null


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()

	if backend and backend.get_script() != null and \
			backend.get_script().get_global_name() == "BackendPeer":
		warnings.append(
			"The assigned backend is the abstract 'BackendPeer' class. " +
			"Please assign a functional derived class.",
		)

	var has_scene_manager := false
	for child in get_children():
		if child is MultiplayerSceneManager:
			has_scene_manager = true
			break

	if not has_scene_manager:
		warnings.append(
			"No MultiplayerSceneManager found as a child. " +
			"No replication will happen.",
		)

	return warnings


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	_mount_api()
	_ensure_interpolation_interface()
	_ensure_host_scene_view()

	# Two-phase debug registration: create the per-tree probe offline (role
	# NONE, peer 0) so the connect lifecycle is observed from the start. The
	# online upgrade (monitors, wire registration) happens in _finalize_session.
	Netw.dbg.register_tree(self)

	if not participant_joined.is_connected(_handle_join_spawn):
		participant_joined.connect(_handle_join_spawn)

	for child in get_children():
		if child is MultiplayerSceneManager:
			return


func _ensure_host_scene_view() -> void:
	if desired_role != Role.LISTEN_SERVER:
		return
	if find_service_node(HostSceneView):
		return
	var view := HostSceneView.new()
	view.name = &"HostSceneView"
	add_child(view)


## Returns the session [ConnectSession], creating it on first access.
##
## Prefer [member NetwMultiplayer.connect] for browser flows. Dedicated and
## headless sessions pay no [ConnectSession], probe manager, or
## [ProviderRegistry] cost until this method is called.
func get_connect_session() -> ConnectSession:
	if Engine.is_editor_hint():
		return null
	var registered := get_service(ConnectSession) as ConnectSession
	if is_instance_valid(registered):
		return registered
	var existing := find_service_node(ConnectSession) as ConnectSession
	if existing:
		register_service(existing)
		return existing
	var session := ConnectSession.new()
	session.name = &"ConnectSession"
	add_child(session) # _ready() auto-binds to this tree
	register_service(session) # discoverable via the service registry
	return session


## Returns the session [NakamaSessionService], creating it on first access.
##
## Mirrors [method get_connect_session]: a registered service wins, else a
## dropped-in node is adopted, else a fresh node is created and registered. Both
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


# Builds a fresh random build tag for the editor "Generate app id" button.
func _random_app_id() -> StringName:
	const CHARS := "abcdefghijklmnopqrstuvwxyz0123456789"
	var out := ""
	for i in 15:
		out += CHARS[randi() % CHARS.length()]
	return StringName(out)


func _init() -> void:
	_auth = AuthCoordinator.new(_roster)
	_auth.set_roster(_roster)
	_auth.set_auth_provider(auth_provider)
	_auth.set_app_tag(_app_tag())
	_auth.set_tree(self)
	_auth.set_server_info_source(server_info_source)
	if not Engine.is_editor_hint():
		api = _make_api()
		tree_exiting.connect(_on_exiting)


# The one construction point for the branch API (a MultiplayerTree whose API
# is not NetwMultiplayer is unrepresentable). Test rigs override this to
# install a capturing NetwMultiplayer subclass.
func _make_api() -> NetwMultiplayer:
	return NetwMultiplayer.new(SceneMultiplayer.new(), self)


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return

	if backend:
		backend.poll(dt)
	if api and api.has_multiplayer_peer():
		api.poll()
	if api:
		api.persistence.tick(dt)


## Starts this tree as a server using [member backend].
##
## [method _open_host] creates transport and sets [member role]. Use
## [method host] when the host should also submit a local
## [JoinPayload].
## [codeblock]
## tree.backend = ENetBackend.new()
##
## var err := await tree._open_host()
## if err == OK:
##     print(tree.role)
## [/codeblock]
func _open_host(quiet: bool = false, options: LobbyDirectory.HostOptions = null) -> Error:
	assert(state == State.OFFLINE, "Must be offline to host.")
	if backend == null:
		if not quiet:
			Netw.dbg.error(
				"MultiplayerTree.host: no backend configured.",
				[],
				func(m): push_error(m)
			)
		return ERR_UNCONFIGURED
	Netw.dbg.trace("MultiplayerTree: Hosting session.")
	_transition(State.CONNECTING)
	backend.peer_reset_state()
	var prior_api := api

	var setup_err: Error = await backend.setup(self)
	if setup_err != OK:
		_transition(State.OFFLINE)
		if not quiet:
			Netw.dbg.error(
				"Setup failed: %s",
				[error_string(setup_err)],
				func(m): push_error(m)
			)
		return setup_err

	_auth.prepare()
	var peer: MultiplayerPeer = await backend.create_host_peer(self, options)

	peer = backend.wrap_peer(peer)
	var api_was_adopted := api != prior_api

	# Adopted-api backends (e.g. TubeBackend) drive their peer onto the swapped
	# api themselves and return null, while non-adopting backends returning null
	# are real failures.
	if peer == null and not api_was_adopted:
		_transition(State.OFFLINE)
		if not quiet:
			Netw.dbg.error(
				"Failed to host: backend produced no peer.",
				func(m): push_error(m)
			)
		return ERR_CANT_CREATE

	if peer != null:
		api.multiplayer_peer = peer

	role = Role.LISTEN_SERVER if desired_role == Role.LISTEN_SERVER \
	else Role.DEDICATED_SERVER
	_transition(State.ONLINE)
	_auth.synthesize_host_identity()
	return OK


## Connects to [param target] and submits [param join_payload].
##
## [member JoinTarget.backend] becomes this tree's live [member backend].
## [member state] reaches [constant State.ONLINE] before
## [method submit_join] runs.
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "valeria"
##
## var target := JoinTarget.new()
## target.backend = ENetBackend.new()
## target.address = "127.0.0.1"
##
## var err := await tree.join(target, payload)
## [/codeblock]
func join(
		target: JoinTarget,
		join_payload: JoinPayload,
		timeout: float = 5.0,
		quiet: bool = false,
) -> Error:
	last_connect_result = null
	assert(state == State.OFFLINE, "Must be offline to join.")

	assert(
		desired_role != Role.DEDICATED_SERVER,
		"join() needs a local player; a dedicated server hosts via host().",
	)
	_join_aborted = false
	if target == null:
		Netw.dbg.error("join: target is null.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER

	var backend_instance := target.make_backend_instance()
	if backend_instance == null:
		Netw.dbg.error(
			"join: target has no backend template.",
			func(m): push_error(m)
		)
		return ERR_INVALID_PARAMETER

	self.backend = backend_instance
	backend_ready_for_join.emit(self.backend)
	var prepare_err := await _prepare_session(join_payload)
	if prepare_err != OK:
		return prepare_err

	var join_err := await _open_join_transport(
		target.address,
		join_payload.username,
		timeout,
		quiet,
	)
	if join_err == OK:
		submit_join(join_payload)
	return join_err


# Opens transport after join payload preparation.
func _open_join_transport(
		server_address: String,
		username: String,
		timeout: float = 5.0,
		quiet: bool = false,
) -> Error:
	Netw.dbg.trace(
		"MultiplayerTree: Joining at %s with username %s.",
		[server_address, username],
	)
	_transition(State.CONNECTING)
	backend.peer_reset_state()
	var prior_api := api

	var setup_err: Error = await backend.setup(self)
	if setup_err != OK:
		_transition(State.OFFLINE)
		if not quiet:
			Netw.dbg.error(
				"Setup failed: %s",
				[error_string(setup_err)],
				func(m): push_error(m)
			)
		return setup_err

	_auth.prepare()
	var peer: MultiplayerPeer = await backend.create_join_peer(
		self,
		server_address,
		username,
	)
	if _join_aborted:
		_transition(State.OFFLINE)
		last_connect_result = BackendPeer.ConnectResult.aborted("Connection aborted by user")
		return ERR_CANT_CONNECT
	peer = backend.wrap_peer(peer)
	var api_was_adopted := api != prior_api

	if peer == null and not api_was_adopted:
		_transition(State.OFFLINE)
		last_connect_result = BackendPeer.ConnectResult.error("Failed to join: backend produced no peer.")
		if not quiet:
			Netw.dbg.error(
				"Failed to join: backend produced no peer.",
				func(m): push_error(m)
			)
		return ERR_CANT_CONNECT

	if peer != null:
		api.multiplayer_peer = peer

	if (peer != null or api_was_adopted) and backend:
		backend.begin_connect_progress(timeout)

	var on_backend_failed := func(res: BackendPeer.ConnectResult) -> void:
		_connect_failed.emit(res)
	var on_api_failed := func() -> void:
		_connect_failed.emit(
			BackendPeer.ConnectResult.unreachable(
				&"PEER_CONNECT_FAILED",
				"Could not reach the server.",
			),
		)

	if backend:
		backend.connect_failed.connect(on_backend_failed, CONNECT_ONE_SHOT)
	if api:
		api.connection_failed.connect(on_api_failed, CONNECT_ONE_SHOT)

	var timer := get_tree().create_timer(timeout)
	var connect_result := await Async.timeout_or_failure(
		connected_to_server,
		_connect_failed,
		timer,
	)

	if backend:
		if backend.connect_failed.is_connected(on_backend_failed):
			backend.connect_failed.disconnect(on_backend_failed)
		backend.end_connect_progress()
	if api:
		if api.connection_failed.is_connected(on_api_failed):
			api.connection_failed.disconnect(on_api_failed)

	var failed_reason_obj: Variant = connect_result.get("reason")
	var did_timeout := String(connect_result.get("result", "")) == "timeout"
	var did_fail := String(connect_result.get("result", "")) == "failure"
	if did_timeout:
		last_connect_result = BackendPeer.ConnectResult.timed_out("Connection timed out")
	elif _join_aborted:
		last_connect_result = BackendPeer.ConnectResult.aborted("Connection aborted by user")
	elif did_fail:
		if failed_reason_obj is BackendPeer.ConnectResult:
			last_connect_result = failed_reason_obj
		else:
			last_connect_result = BackendPeer.ConnectResult.error(str(failed_reason_obj))

	if did_timeout or did_fail or _join_aborted:
		_transition(State.OFFLINE)
		if not quiet and not _join_aborted:
			var message := "Connection timed out. Server probably is not up."
			if did_fail and last_connect_result != null:
				message = "Connection failed: %s." % (
						last_connect_result.message
						if not last_connect_result.message.is_empty()
						else str(last_connect_result)
				)
			Netw.dbg.error(
				message,
				func(m): push_error(m)
			)
		return ERR_CANT_CONNECT

	last_connect_result = BackendPeer.ConnectResult.ok()
	if backend:
		last_connect_result.diagnostics = (
				backend.get_connection_diagnostics(1)
		)
	role = Role.CLIENT
	_transition(State.ONLINE)
	return OK


## Joins [param target], or hosts when no listener replies.
##
## [method BackendPeer.probe_server_info] decides between [method join] and
## [method host_player]. Backends where
## [method BackendPeer.supports_embedded_server] returns [code]false[/code]
## always use [method host_player].
## [codeblock]
## var err := await tree.join_or_host(target, payload)
## if err == OK and tree.is_host:
##     print("Hosting")
## [/codeblock]
func join_or_host(
		target: JoinTarget,
		join_payload: JoinPayload,
) -> Error:
	assert(state == State.OFFLINE, "Must be offline to connect.")
	assert(
		desired_role != Role.DEDICATED_SERVER,
		"join_or_host() needs a local player; a dedicated server hosts via host().",
	)
	if target == null:
		Netw.dbg.error("join_or_host: target is null.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER

	var backend_instance := target.make_backend_instance()
	if backend_instance == null:
		Netw.dbg.error(
			"join_or_host: target has no backend template.",
			func(m): push_error(m)
		)
		return ERR_INVALID_PARAMETER

	self.backend = backend_instance

	if not self.backend.supports_embedded_server():
		var host_err := await _open_host(true)
		if host_err == OK:
			role = Role.LISTEN_SERVER
			await host_ready
			submit_join(join_payload)
		return host_err

	var result: BackendPeer.ProbeResult = await self.backend.probe_server_info(
		target.address,
		0.2,
	)
	if result.is_ok():
		Netw.dbg.debug(
			"join_or_host: live listener (%s); joining.",
			[result],
		)
		return await join(target, join_payload)

	Netw.dbg.debug(
		"join_or_host: no live listener (%s); hosting.",
		[result],
	)
	return await host(join_payload)


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	return (api != null
			and api.has_multiplayer_peer()
			and not api.multiplayer_peer is OfflineMultiplayerPeer
			and api.multiplayer_peer.get_connection_status()
			== MultiplayerPeer.CONNECTION_CONNECTED)


## Aborts [constant State.CONNECTING] and returns to
## [constant State.OFFLINE].
func abort_join() -> void:
	if state != State.CONNECTING:
		return
	_join_aborted = true
	Netw.dbg.info("MultiplayerTree: aborting connection handshake.")
	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()
		api.multiplayer_peer = OfflineMultiplayerPeer.new()
	_transition(State.OFFLINE)


## Flushes local save state and closes [member multiplayer_peer].
##
## [member state] returns to [constant State.OFFLINE] before this method
## completes. [signal session_ended] fires on the way out so session-scoped
## subscribers tear down.
func leave() -> void:
	if state == State.OFFLINE:
		return

	Netw.dbg.trace("MultiplayerTree: leave called.")
	Netw.dbg.info("Disconnecting player.")

	# Save before the transition so [method _teardown_session] does not despawn
	# the player scenes out from under the save pass.
	if api:
		api.persistence.flush_all()

	_transition(State.DISCONNECTING)

	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()

	var timer := get_tree().create_timer(3.0)
	if api:
		await Async.timeout(api.server_disconnected, timer)

	_transition(State.OFFLINE)


## Pauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	assert(is_host, "MultiplayerTree.pause() must be called on the server.")
	for peer_id: int in api.get_peers():
		_rpc_receive_pause.rpc_id(peer_id, reason)
	_rpc_receive_pause(reason)


## Unpauses the game on every peer.
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	assert(is_host, "MultiplayerTree.unpause() must be called on the server.")
	for peer_id: int in api.get_peers():
		_rpc_receive_unpause.rpc_id(peer_id)
	_rpc_receive_unpause()


## Disconnects [param peer_id] from the session.
##
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	assert(is_host, "MultiplayerTree.kick() must be called on the server.")
	if not reason.is_empty():
		_rpc_receive_kicked.rpc_id(peer_id, reason)
	if multiplayer_peer:
		multiplayer_peer.disconnect_peer(peer_id)


## Asks the server to kick [param peer_id].
##
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	_rpc_request_kick.rpc_id(1, peer_id, reason)


## Asks the server for permission to leave.
##
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	_rpc_request_leave.rpc_id(1, reason)


## Notifies all clients that the server is shutting down.
##
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	assert(
		is_host,
		"MultiplayerTree.notify_shutdown() must be called on the server.",
	)
	for peer_id: int in api.get_peers():
		_rpc_receive_notify_shutdown.rpc_id(peer_id, reason)
	_rpc_receive_notify_shutdown.rpc_id(1, reason)


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
	assert(state == State.OFFLINE, "Must be offline to host.")
	assert(
		desired_role != Role.DEDICATED_SERVER,
		"host() needs a local player; a dedicated server hosts via _open_host().",
	)
	var err := await _prepare_session(join_payload)
	if err != OK:
		return err

	return await _host_player_logic(join_payload, options)


func _prepare_session(join_payload: JoinPayload) -> Error:
	if not join_payload:
		Netw.dbg.error(
			"join_payload is null.",
			func(m): push_error(m)
		)
		return ERR_INVALID_PARAMETER
	if join_payload.username.is_empty():
		Netw.dbg.error(
			"username is empty.",
			func(m): push_error(m)
		)
		return ERR_INVALID_PARAMETER

	var prepare_err := await _auth.prepare_join_payload(join_payload)
	if prepare_err != OK:
		return prepare_err

	_client_join_payload = join_payload
	_auth.set_client_join_payload(join_payload)
	await leave()
	return OK


func _host_player_logic(
		join_payload: JoinPayload,
		options: LobbyDirectory.HostOptions = null,
) -> Error:
	# LISTEN_SERVER and NONE host on this tree; CLIENT spins up an embedded
	# dedicated sibling and joins it.
	if desired_role != Role.CLIENT:
		var host_err := await _open_host(true, options)

		if host_err == OK:
			role = Role.LISTEN_SERVER
			if get_service(MultiplayerSceneManager):
				await host_ready
			submit_join(join_payload)
			return OK
		elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
			var join_err := await _open_join_transport(
				backend.get_join_address(),
				join_payload.username,
			)
			if join_err == OK:
				submit_join(join_payload)
			return join_err
		else:
			return host_err

	var server := duplicate() as MultiplayerTree
	server.desired_role = Role.DEDICATED_SERVER
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
		var join_err := await _open_join_transport(
			server.backend.get_join_address(),
			join_payload.username,
		)
		if join_err == OK:
			submit_join(join_payload)
		return join_err
	elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
		server.queue_free.call_deferred()
		var join_err := await _open_join_transport(
			backend.get_join_address(),
			join_payload.username,
		)
		if join_err == OK:
			submit_join(join_payload)
		return join_err
	else:
		server.queue_free.call_deferred()
		return host_err


## Submits [param join_payload] through [method request_join].
func submit_join(join_payload: JoinPayload) -> void:
	request_join.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize(),
	)


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

	if auto_host_headless and backend != null \
			and DisplayServer.get_name() == "headless":
		# host() resolves the live role from desired_role.
		if desired_role == Role.LISTEN_SERVER \
				or desired_role == Role.DEDICATED_SERVER:
			await _open_host()
		return

	if debug_join != null and backend != null \
			and desired_role != Role.DEDICATED_SERVER \
			and OS.has_feature("debug"):
		await _debug_autoconnect()


# Restores the old init_payload_debug flow: host straight into the game from
# the editor, no ConnectBrowser. Debug-only, so release never auto-connects.
func _debug_autoconnect() -> void:
	if state != State.OFFLINE:
		return
	var payload := debug_join.to_payload()
	Netw.dbg.info(
		"MultiplayerTree: debug auto-connect as '%s'.",
		[payload.username],
	)
	await host(payload)


## Server RPC that accepts serialized join requests.
##
## [param bytes] must contain [method JoinPayload.serialize] data. Accepted
## joins update [method get_participants] and emit [signal participant_joined].
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "valeria"
## tree.submit_join(payload)
## [/codeblock]
@rpc("any_peer", "call_local", "reliable")
func request_join(bytes: PackedByteArray) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"request_join received on non-server peer %d",
			[multiplayer.get_unique_id()],
		)
		return
	var peer_id := multiplayer.get_remote_sender_id()

	var join_payload: JoinPayload = JoinPayload.new()
	join_payload.deserialize(bytes)
	join_payload.peer_id = peer_id

	_auth.resolve_identity(peer_id, join_payload)

	var rj := join_payload.resolve()
	if not rj:
		Netw.dbg.warn(
			"request_join: invalid payload from peer %d",
			[peer_id],
		)
		return

	if not _resolve_username_collision(rj):
		return

	_remember_accepted_join(rj)
	_rpc_notify_participant_joined.rpc(rj.serialize())
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		_rpc_sync_accepted_participants.rpc_id(
			peer_id,
			_serialize_accepted_participants(),
		)


# Emits the accepted join notification on remote peers.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_notify_participant_joined(bytes: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		Netw.dbg.warn(
			"_rpc_notify_participant_joined received from non-server peer %d",
			[sender],
		)
		return

	var rj := ResolvedJoin.deserialize(bytes)
	_remember_accepted_join(rj)


# Sends all accepted participant payloads to a newly accepted peer.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_sync_accepted_participants(payloads: Array[PackedByteArray]) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		Netw.dbg.warn(
			"_rpc_sync_accepted_participants from non-server peer %d",
			[sender],
		)
		return

	for bytes: PackedByteArray in payloads:
		var rj := ResolvedJoin.deserialize(bytes)
		_remember_accepted_join(rj)


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


# Runs spawn_policy for accepted participants.
func _handle_join_spawn(participant: NetwParticipant) -> void:
	if not multiplayer.is_server():
		return
	if spawn_policy == null:
		return
	var rj := participant.join
	if rj == null:
		return
	if rj.spawn.is_empty():
		return
	_assert_spawn_policy_matches(rj)
	var scene := await spawn_policy.spawn(rj, api)
	if scene:
		participant.current_scene = scene.netw_scene


func _assert_spawn_policy_matches(rj: ResolvedJoin) -> void:
	if rj.spawn.is_empty():
		return
	var expected := spawn_policy.policy_script_identifier()
	var actual := str(rj.spawn.get(SpawnPolicy._POLICY_SCRIPT_KEY, ""))
	assert(
		actual == expected,
		(
				"spawn policy mismatch. Sender and receiver are using " +
				"different SpawnPolicies"
		),
	)


# Stores accepted participant data and emits it once on this peer.
func _remember_accepted_join(rj: ResolvedJoin) -> bool:
	if _roster.remember_accepted_join(rj):
		var participant := get_participant(rj.peer_id)
		_emit_participant_joined(participant)
		return true
	return false


# Serializes the locally known accepted participant roster.
func _serialize_accepted_participants() -> Array[PackedByteArray]:
	return _roster.serialize_accepted_joins()

# Pause RPC handlers.


@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_pause(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		Netw.dbg.warn(
			"_rpc_receive_pause received from non-server peer %d",
			[sender],
		)
		return
	get_tree().paused = true
	tree_paused.emit(reason)


@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_unpause() -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		Netw.dbg.warn(
			"_rpc_receive_unpause received from non-server peer %d",
			[sender],
		)
		return
	get_tree().paused = false
	tree_unpaused.emit()

# Kick RPC handlers.


# Receives the server kick notification on the target peer.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_kicked(reason: String) -> void:
	kicked.emit(reason)


# Receives a client kick request on the server.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_kick(target_peer_id: int, reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"_rpc_request_kick received on non-server peer %d",
			[multiplayer.get_unique_id()],
		)
		return
	var requester_id := multiplayer.get_remote_sender_id()
	kick_requested.emit(requester_id, target_peer_id, reason)

# Disconnect RPC handlers.


# Receives a client leave request on the server.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_leave(reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"_rpc_request_leave received on non-server peer %d",
			[multiplayer.get_unique_id()],
		)
		return
	var peer_id := multiplayer.get_remote_sender_id()
	disconnect_requested.emit(peer_id, reason)


# Receives the server shutdown notice on clients.
@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_notify_shutdown(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1:
		Netw.dbg.warn(
			"_rpc_receive_notify_shutdown received from non-server peer %d",
			[sender],
		)
		return
	server_disconnecting.emit(reason)


# Returns true when the join should proceed.
func _resolve_username_collision(rj: ResolvedJoin) -> bool:
	return _roster.resolve_username_collision(
		rj,
		get_all_players(),
		api.inner.disconnect_peer if api else Callable(),
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


func _ensure_interpolation_interface() -> void:
	_interpolation_interface = _ensure_service(
		NetwInterpolationInterface,
		&"NetwInterpolationInterface",
		_interpolation_interface,
	) as NetwInterpolationInterface


# Resolves the child service of [param script_type], reusing an already
# mounted node, adopting one found by [method find_service_node], re-parenting
# the transient [param current] copied by [method Node.duplicate], or creating a
# fresh instance named [param node_name]. The transient copy is freed when a
# mounted node wins.
func _ensure_service(
		script_type: Script,
		node_name: StringName,
		current: Node,
) -> Node:
	if is_instance_valid(current) and is_ancestor_of(current):
		return current

	var existing := get_node_or_null(NodePath(node_name))
	if not is_instance_of(existing, script_type):
		existing = find_service_node(script_type)
	if existing:
		if is_instance_valid(current) \
				and current != existing \
				and current.get_parent() == null:
			current.free()
		return existing

	if is_instance_valid(current) and current.get_parent() == null:
		add_child(current)
		return current

	var created := script_type.new() as Node
	created.name = node_name
	add_child(created)
	return created


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
	_auth.bind_api(api.inner)


# Validates the edge against [constant _LEGAL_EDGES] and runs the exit hook for
# the old state then the enter hook for the new one. The only caller allowed to
# move [member state] so setup and teardown stay paired.
func _transition(next: State) -> void:
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


# Runs the setup half on entering a state. ONLINE finalizes the live session.
func _on_enter_state(next: State) -> void:
	match next:
		State.ONLINE:
			_finalize_session()


# Runs the teardown half on leaving a state. Leaving ONLINE tears the session
# down so it never fires on a failed connect (CONNECTING -> OFFLINE).
func _on_exit_state(prev: State) -> void:
	match prev:
		State.ONLINE:
			_teardown_session()


# Finalizes the session once the peer is live and the role is set.
func _finalize_session() -> void:
	Netw.dbg.trace("MultiplayerTree: Finalizing session.")
	Netw.dbg.debug(
		"MultiplayerTree: session app_id='%s' app_tag=0x%08x.",
		[String(app_id), _app_tag()],
	)
	Netw.dbg.finalize_tree(self)
	session_entered.emit()

	var sm := get_service(MultiplayerSceneManager)
	if sm and not sm.startup_scenes_spawned.is_connected(host_ready.emit):
		sm.startup_scenes_spawned.connect(host_ready.emit)


# Mirror of [method _finalize_session]. Releases the session so session-scoped
# subscribers unwind and a same-tree re-host starts clean. The embedded Server
# sibling spun up by _host_player_logic is freed here too.
func _teardown_session() -> void:
	if local_participant and local_participant.current_scene:
		local_participant.current_scene = null
	local_participant = null
	session_ended.emit()
	_roster.clear()
	_participants.clear()
	role = Role.NONE

	var parent := get_parent()
	if parent:
		var server := parent.get_node_or_null("Server") as MultiplayerTree
		if server and server != self:
			server.queue_free.call_deferred()


func _bind_api_signals(target: NetwMultiplayer) -> void:
	if not target:
		return
	_auth.bind_api(target.inner)
	# local_player follows the liveness bus: the represented entity is the one
	# whose route goes live carrying the local peer id, cleared when that route
	# dies. Riding the bus (rather than a per-tree-entry write) drops the
	# clear-and-reset flicker a reparent used to cause, since a reparent keeps
	# the route live and never emits entity_dead.
	if not target.liveness.entity_live.is_connected(_on_liveness_entity_live):
		target.liveness.entity_live.connect(_on_liveness_entity_live)
	if not target.liveness.entity_dead.is_connected(_on_liveness_entity_dead):
		target.liveness.entity_dead.connect(_on_liveness_entity_dead)
	if not target.peer_connected.is_connected(_on_peer_connected):
		target.peer_connected.connect(_on_peer_connected)
	if not target.peer_disconnected.is_connected(_on_peer_disconnected):
		target.peer_disconnected.connect(_on_peer_disconnected)
	if not target.connected_to_server.is_connected(_on_connected_to_server):
		target.connected_to_server.connect(_on_connected_to_server)
	if not target.server_disconnected.is_connected(_on_server_disconnected):
		target.server_disconnected.connect(_on_server_disconnected)


func _unbind_api_signals(target: NetwMultiplayer) -> void:
	if not target:
		return
	_auth.bind_api(null)
	if target.liveness.entity_live.is_connected(_on_liveness_entity_live):
		target.liveness.entity_live.disconnect(_on_liveness_entity_live)
	if target.liveness.entity_dead.is_connected(_on_liveness_entity_dead):
		target.liveness.entity_dead.disconnect(_on_liveness_entity_dead)
	if target.peer_connected.is_connected(_on_peer_connected):
		target.peer_connected.disconnect(_on_peer_connected)
	if target.peer_disconnected.is_connected(_on_peer_disconnected):
		target.peer_disconnected.disconnect(_on_peer_disconnected)
	if target.connected_to_server.is_connected(_on_connected_to_server):
		target.connected_to_server.disconnect(_on_connected_to_server)
	if target.server_disconnected.is_connected(_on_server_disconnected):
		target.server_disconnected.disconnect(_on_server_disconnected)


# Adopts a newly live entity as local_player when it represents the local peer.
# A session-less peer (no multiplayer_peer) has no local player, matching the
# old represented-peer test that treated a null peer as not-local.
func _on_liveness_entity_live(_route: int, entity: NetwEntity) -> void:
	if not api or api.multiplayer_peer == null:
		return
	if entity.peer_id == 0 or entity.peer_id != api.get_unique_id():
		return
	local_player = entity


func _on_liveness_entity_dead(route: int) -> void:
	var current := local_player
	if current and current.route == route:
		local_player = null


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

	if backend:
		backend.peer_reset_state()

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

	if backend:
		backend.peer_reset_state()

	dispose()


func _on_peer_connected(peer_id: int) -> void:
	Netw.dbg.info("Peer connected: %d", [peer_id])
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	Netw.dbg.info("Peer disconnected: %d", [peer_id])
	var participant := _participants.get(peer_id, null) as NetwParticipant
	if participant and participant.current_scene:
		participant.current_scene = null
	_roster.forget_peer(peer_id)
	_participants.erase(peer_id)
	peer_disconnected.emit(peer_id)


func _on_local_participant_scene_changed(
		from: NetwScene,
		to: NetwScene,
) -> void:
	local_scene_changed.emit(from, to)


func _sync_local_participant_scene() -> void:
	if local_participant == null or api == null:
		return
	var sm := get_service(MultiplayerSceneManager) as MultiplayerSceneManager
	if sm == null:
		return
	for scene: MultiplayerScene in sm.active_scenes.values():
		var layer := api.interest.get_layer(scene.scene_layer_id())
		if layer and layer.viewers.has(local_participant.peer_id):
			local_participant.current_scene = scene.netw_scene
			return


func _notify_local_scene_released(
		peer_id: int,
		scene_layer_id: StringName,
) -> void:
	if peer_id == multiplayer.get_unique_id():
		_rpc_clear_local_scene(scene_layer_id)
	else:
		rpc_id(peer_id, "_rpc_clear_local_scene", scene_layer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	Netw.dbg.info("Connected to server as peer %d.", [peer_id])

	_auth.on_connected_to_server()

	set_multiplayer_authority(peer_id, false)
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	Netw.dbg.info("Disconnected from server.")
	server_disconnected.emit()

	# A server crash routes through the same teardown as a graceful leave. The
	# ONLINE guard makes a crash arriving mid-leave a no-op.
	if state == State.ONLINE:
		_transition(State.DISCONNECTING)
		_transition(State.OFFLINE)


@rpc("any_peer", "call_local", "reliable")
func _rpc_clear_local_scene(scene_layer_id: StringName) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		Netw.dbg.warn(
			"_rpc_clear_local_scene received from non-server peer %d",
			[sender],
		)
		return
	if local_participant == null:
		return
	var current := local_participant.current_scene
	if current == null or current.unwrap() == null:
		return
	if current.unwrap().scene_layer_id() == scene_layer_id:
		local_participant.current_scene = null
