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
## [method disconnect_player] or the server-crash path, never on a failed
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

## Emitted on every peer after the server accepts a player join.
signal player_joined(rj: ResolvedJoin)

## Emitted when this peer's player join has been accepted by the server.
signal local_player_joined(rj: ResolvedJoin)

## Emitted after the host's startup scenes have been spawned and the server
## is ready to accept the local player. Only relevant for listen server hosts.
signal host_ready()

## Emitted when the connection state changes.
signal state_changed(old_state: State, new_state: State)

## Emitted after [member backend] is cloned for a client join attempt.
signal backend_ready_for_join(backend: BackendPeer)

## Emitted when [member api] is replaced.
##
## Both [param old_api] and [param new_api] may be valid. Consumers that cached
## [member api] should rebind.
signal api_swapped(
		old_api: SceneMultiplayer,
		new_api: SceneMultiplayer,
		reason: String,
)

# Internal signal to relay connection failure outcomes.
signal _connect_failed(result: ConnectResult)

## Session lifecycle state for this tree.
## [codeblock]
## OFFLINE
##   | host() / join() / host_player()
##   v
## CONNECTING --(failure or abort_join)--> OFFLINE
##   | success
##   v
## ONLINE
##   | disconnect_player()
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

## Optional [NetwAuthProvider] for [method join] and [method join_or_host].
##
## A [code]null[/code] provider skips authentication. The server trusts the
## client supplied [member JoinPayload.username].
@export var auth_provider: NetwAuthProvider:
	set(value):
		auth_provider = value
		if _auth:
			_auth.set_auth_provider(value)
			_auth.prepare()

## Outcome of the last connection handshake.
var last_connect_result: ConnectResult = null

@export_group("Session")

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

## Builds [ServerInfo] for [method BackendPeer.query_server_info]. When this
## member is [code]null[/code], [AuthProbeResponder] creates a
## [DefaultServerInfoSource] while answering a probe.
##
## [DefaultServerInfoSource] derives live values from this tree.
## [codeblock]
##     func build_server_info(tree: MultiplayerTree) -> ServerInfo:
##         var info := ServerInfo.new()
##         info.players = tree.get_joined_players().size()
##         info.app_id = tree.app_id
##         return info
## [/codeblock]
## Read [ServerInfoSource] and [DefaultServerInfoSource] before assigning a
## custom source.
@export var server_info_source: ServerInfoSource:
	set(value):
		server_info_source = value
		if _auth:
			_auth.set_server_info_source(value)

## Server side [SpawnPolicy] for accepted joins.
##
## A [code]null[/code] value means [signal player_joined] is the gameplay
## entry point. If the tree has no [MultiplayerSceneManager] but does
## have a child scene with a [MultiplayerEntity], [method _enter_tree] creates a
## [EntitySpawnPolicy].
## [codeblock]
## # Client. Store spawn intent in JoinPayload.spawn.
## payload.spawn = spawn_policy.to_dict()
##
## # Server. MultiplayerTree calls spawn after accepting the join.
## var scene := await spawn_policy.spawn(rj, Netw.ctx(tree))
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

## Owned [SceneMultiplayer] mounted for this session.
##
## Backends may replace it through [signal api_swapped]. Consumers that cache
## [member api] should rebind when that signal fires.
var api: SceneMultiplayer

## Visibility and interest facade for this tree.
##
## [member interest] is backed by the session [InterestService].
var interest: NetwInterest

## Deprecated compatibility alias for [member api].
var multiplayer_api: SceneMultiplayer:
	get:
		return api

## The active [MultiplayerPeer] connection.
var multiplayer_peer: MultiplayerPeer:
	get:
		return api.multiplayer_peer if api else null

var _tree_name: String = ""
var _join_aborted: bool = false
var _deletion_finalized: bool = false

## Local player [Node] for this tree, or [code]null[/code].
##
## [signal local_player_changed] fires whenever this member changes.
var local_player: Node:
	set(value):
		if local_player != value:
			local_player = value
			local_player_changed.emit(value)

## Emitted when [member local_player] is assigned or cleared.
signal local_player_changed(player: Node)

## Emitted after [member spawn_policy] places a player in a scene.
signal player_scene_ready(
		rj: ResolvedJoin,
		scene: MultiplayerScene,
)

## Emitted on every peer when the game is paused via [method NetwTree.pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via
## [method NetwTree.unpause].
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
## [SceneMultiplayer].
static func for_node(node: Node) -> MultiplayerTree:
	if node is MultiplayerTree:
		return node
	var api := node.multiplayer as SceneMultiplayer
	if not api or not api.has_meta(&"_multiplayer_tree"):
		return null
	return api.get_meta(&"_multiplayer_tree") as MultiplayerTree


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
var _auth: AuthCoordinator
var _services: ServiceRegistry = ServiceRegistry.new()
var _client_join_payload: JoinPayload
var _interest_service: InterestService


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
	_roster.clear()
	_client_join_payload = null


## Returns the [NetwPeerContext] for [param peer_id], creating one on first
## access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	return _roster.get_peer_context(peer_id)


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func has_peer_context(peer_id: int) -> bool:
	return _roster.has_peer_context(peer_id)


## Returns accepted player join data known by this peer.
func get_joined_players() -> Array[ResolvedJoin]:
	return _roster.get_joined_players()


## Returns the accepted player data for [param peer_id], or
## [code]null[/code].
func get_joined_player(peer_id: int) -> ResolvedJoin:
	return _roster.get_joined_player(peer_id)


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


## Returns active player [Node]s across every [MultiplayerScene].
func get_all_players() -> Array[Node]:
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
	var has_sceneless_world := false
	for child in get_children():
		if child is MultiplayerSceneManager:
			has_scene_manager = true
			break
		if _has_multiplayer_entity(child):
			has_sceneless_world = true
			break

	if not has_scene_manager and not has_sceneless_world:
		warnings.append(
			"No world scene (containing a MultiplayerEntity) or " +
			"MultiplayerSceneManager found as a child. " +
			"No replication will happen.",
		)

	return warnings


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	_mount_api()
	_ensure_interest_service()
	_ensure_host_scene_view()

	if not player_joined.is_connected(_handle_join_spawn):
		player_joined.connect(_handle_join_spawn)

	for child in get_children():
		if child is MultiplayerSceneManager:
			return

	for child in get_children():
		if _has_multiplayer_entity(child):
			var scene_path := child.scene_file_path
			if scene_path.is_empty():
				push_error(
					"[networked] World '%s' must be a saved .tscn." % child.name,
				)
				return
			Netw.dbg.info(
				"Default scene: using '%s' as the session world.",
				[child.name],
			)
			remove_child(child)
			child.queue_free()
			var manager := MultiplayerSceneManager.new()
			manager.name = &"SceneManager"
			# Zero-config world: auto-spawn joining players at the picked
			# MultiplayerEntity. An explicitly placed tree defaults to no
			# policy and leaves spawning to gameplay.
			if spawn_policy == null:
				spawn_policy = EntitySpawnPolicy.new()
			add_child(manager)
			manager._configure_default(scene_path)
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
## Prefer [member NetwContext.connect] for browser flows. Dedicated and
## headless sessions pay no [ConnectSession], [ProbeManager], or
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


static func _has_multiplayer_entity(node: Node) -> bool:
	if node is MultiplayerEntity:
		return true
	for child in node.get_children():
		if _has_multiplayer_entity(child):
			return true
	return false


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
		api = SceneMultiplayer.new()
		_interest_service = InterestService.new()
		_interest_service.name = &"InterestService"
		interest = NetwInterest.new(self)
		tree_exiting.connect(_on_exiting)


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return

	if backend:
		backend.poll(dt)
	if api and api.has_multiplayer_peer():
		api.poll()


## Starts this tree as a server using [member backend].
##
## [method host] creates transport and sets [member role]. Use
## [method host_player] when the host should also submit a local
## [JoinPayload].
## [codeblock]
## tree.backend = ENetBackend.new()
##
## var err := await tree.host()
## if err == OK:
##     print(tree.role)
## [/codeblock]
func host(quiet: bool = false) -> Error:
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
	var peer: MultiplayerPeer = await backend.create_host_peer(self)
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
		last_connect_result = ConnectResult.aborted("Connection aborted by user")
		return ERR_CANT_CONNECT
	peer = backend.wrap_peer(peer)
	var api_was_adopted := api != prior_api

	if peer == null and not api_was_adopted:
		_transition(State.OFFLINE)
		last_connect_result = ConnectResult.error("Failed to join: backend produced no peer.")
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

	var on_backend_failed := func(res: ConnectResult) -> void:
		_connect_failed.emit(res)
	var on_api_failed := func() -> void:
		_connect_failed.emit(
			ConnectResult.unreachable(
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
		last_connect_result = ConnectResult.timed_out("Connection timed out")
	elif _join_aborted:
		last_connect_result = ConnectResult.aborted("Connection aborted by user")
	elif did_fail:
		if failed_reason_obj is ConnectResult:
			last_connect_result = failed_reason_obj
		else:
			last_connect_result = ConnectResult.error(str(failed_reason_obj))

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

	last_connect_result = ConnectResult.ok()
	if backend:
		last_connect_result.diagnostics = (
				backend.get_connection_diagnostics(1)
		)
	role = Role.CLIENT
	_transition(State.ONLINE)
	return OK


## Joins [param target], or hosts when no listener replies.
##
## [method BackendPeer.query_server_info] decides between [method join] and
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
		var host_err := await host(true)
		if host_err == OK:
			role = Role.LISTEN_SERVER
			await host_ready
			submit_join(join_payload)
		return host_err

	var result: ServerInfoResult = await self.backend.query_server_info(
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
	return await host_player(join_payload)


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
func disconnect_player() -> void:
	if state == State.OFFLINE:
		return

	Netw.dbg.trace("MultiplayerTree: disconnect_player called.")
	Netw.dbg.info("Disconnecting player.")

	# Save before the transition so [method _teardown_session] does not despawn
	# the player scenes out from under the save pass.
	var peer_id := api.get_unique_id() if api else 0
	if peer_id != 0:
		SaveComponent._save_all_in(get_peer_context(peer_id))

	_transition(State.DISCONNECTING)

	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()

	var timer := get_tree().create_timer(3.0)
	if api:
		await Async.timeout(api.server_disconnected, timer)

	_transition(State.OFFLINE)


## Hosts a session and submits the local [param join_payload].
##
## Use [method join_or_host] when the caller should probe before hosting.
## [method host_player] is the direct host path.
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "Host"
##
## var err := await tree.host_player(payload)
## [/codeblock]
func host_player(join_payload: JoinPayload) -> Error:
	assert(state == State.OFFLINE, "Must be offline to host.")
	assert(
		desired_role != Role.DEDICATED_SERVER,
		"host_player() needs a local player; a dedicated server hosts via host().",
	)
	var err := await _prepare_session(join_payload)
	if err != OK:
		return err

	return await _host_player_logic(join_payload)


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
	await disconnect_player()
	return OK


func _host_player_logic(join_payload: JoinPayload) -> Error:
	# LISTEN_SERVER and NONE host on this tree; CLIENT spins up an embedded
	# dedicated sibling and joins it.
	if desired_role != Role.CLIENT:
		var host_err := await host(true)
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

	var host_err := await server.host(true)
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


## Submits [param join_payload] through [method request_join_player].
func submit_join(join_payload: JoinPayload) -> void:
	request_join_player.rpc_id(
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
			await host()
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
	await host_player(payload)


## Server RPC that accepts serialized join requests.
##
## [param bytes] must contain [method JoinPayload.serialize] data. Accepted
## joins update [method get_joined_players] and emit [signal player_joined].
## [codeblock]
## var payload := JoinPayload.new()
## payload.username = "valeria"
## tree.submit_join(payload)
## [/codeblock]
@rpc("any_peer", "call_local", "reliable")
func request_join_player(bytes: PackedByteArray) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"request_join_player received on non-server peer %d",
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
			"request_join_player: invalid payload from peer %d",
			[peer_id],
		)
		return

	if not _resolve_username_collision(rj):
		return

	_remember_joined_player(rj)
	_rpc_notify_player_joined.rpc(rj.serialize())
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		_rpc_sync_joined_players.rpc_id(peer_id, _serialize_joined_players())


# Emits the accepted join notification on remote peers.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_notify_player_joined(bytes: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		Netw.dbg.warn(
			"_rpc_notify_player_joined received from non-server peer %d",
			[sender],
		)
		return

	var rj := ResolvedJoin.deserialize(bytes)
	_remember_joined_player(rj)


# Sends all accepted player payloads to a newly joined peer.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_sync_joined_players(payloads: Array[PackedByteArray]) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		Netw.dbg.warn(
			"_rpc_sync_joined_players received from non-server peer %d",
			[sender],
		)
		return

	for bytes: PackedByteArray in payloads:
		var rj := ResolvedJoin.deserialize(bytes)
		_remember_joined_player(rj)


# Emits join signals derived from the accepted server authority data.
func _emit_player_joined(rj: ResolvedJoin) -> void:
	player_joined.emit(rj)

	if rj.peer_id == multiplayer.get_unique_id():
		local_player_joined.emit(rj)


# Runs spawn_policy for accepted joins.
func _handle_join_spawn(rj: ResolvedJoin) -> void:
	if not multiplayer.is_server():
		return
	if spawn_policy == null:
		return
	var scene := await spawn_policy.spawn(rj, Netw.ctx(self))
	if scene:
		player_scene_ready.emit(rj, scene)


# Stores resolved join data and emits it once on this peer.
func _remember_joined_player(rj: ResolvedJoin) -> bool:
	if _roster.remember_joined_player(rj):
		_emit_player_joined(rj)
		return true
	return false


# Serializes the locally known accepted player roster.
func _serialize_joined_players() -> Array[PackedByteArray]:
	return _roster.serialize_joined_players()

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


# Receives a client disconnect request on the server.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_disconnect(reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"_rpc_request_disconnect received on non-server peer %d",
			[multiplayer.get_unique_id()],
		)
		return
	var peer_id := multiplayer.get_remote_sender_id()
	disconnect_requested.emit(peer_id, reason)


## Broadcasts a server shutdown notice to all connected clients.
func notify_shutdown(reason: String) -> void:
	_rpc_receive_notify_disconnect.rpc(reason)


# Receives the server shutdown notice on clients.
@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_notify_disconnect(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1:
		Netw.dbg.warn(
			"_rpc_receive_notify_disconnect received from non-server peer %d",
			[sender],
		)
		return
	server_disconnecting.emit(reason)


# Returns true when the join should proceed.
func _resolve_username_collision(rj: ResolvedJoin) -> bool:
	return _roster.resolve_username_collision(
		rj,
		get_all_players(),
		api.disconnect_peer if api else Callable(),
	)


# Mounts the owned api onto the SceneTree path and binds signals.
func _mount_api() -> void:
	if not api:
		return

	_tree_name = name
	var root_path := get_path()
	api.root_path = root_path
	get_tree().set_multiplayer(api, root_path)
	api.set_meta(&"_multiplayer_tree", self)
	_bind_api_signals(api)


func _ensure_interest_service() -> void:
	if is_instance_valid(_interest_service) \
			and is_ancestor_of(_interest_service):
		return

	var existing := get_node_or_null("InterestService") \
			as InterestService
	if not existing:
		existing = find_service_node(InterestService) \
				as InterestService
	if existing:
		_free_unparented_interest_service(existing)
		_interest_service = existing
		return

	if is_instance_valid(_interest_service) \
			and _interest_service.get_parent() == null:
		add_child(_interest_service)
		return

	_interest_service = InterestService.new()
	_interest_service.name = &"InterestService"
	add_child(_interest_service)


# Frees the transient service copied by duplicate().
func _free_unparented_interest_service(keep: InterestService) -> void:
	if not is_instance_valid(_interest_service):
		return
	if _interest_service == keep:
		return
	if _interest_service.get_parent() != null:
		return
	_interest_service.free()


# Installs a placeholder api because Godot rejects null at scoped paths.
func _unmount_api(release_meta: bool) -> void:
	if not api:
		return

	_unbind_api_signals(api)
	if release_meta and api.has_meta(&"_multiplayer_tree"):
		api.remove_meta(&"_multiplayer_tree")

	if not api.root_path.is_empty():
		get_tree().set_multiplayer(SceneMultiplayer.new(), api.root_path)


# Replaces the owned api for backends that bring a SceneMultiplayer.
func _adopt_api(new_api: SceneMultiplayer, reason: String) -> void:
	if new_api == api:
		return

	var old_api := api
	if old_api:
		_unbind_api_signals(old_api)
		if old_api.has_meta(&"_multiplayer_tree"):
			old_api.remove_meta(&"_multiplayer_tree")
		if not old_api.root_path.is_empty():
			get_tree().set_multiplayer(SceneMultiplayer.new(), old_api.root_path)

	api = new_api
	if api:
		var root_path := get_path()
		api.root_path = root_path
		get_tree().set_multiplayer(api, root_path)
		api.set_meta(&"_multiplayer_tree", self)
		_bind_api_signals(api)

	api_swapped.emit(old_api, api, reason)


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
	Netw.dbg.register_tree(self)
	session_entered.emit()

	var sm := get_service(MultiplayerSceneManager)
	if sm and not sm.startup_scenes_spawned.is_connected(host_ready.emit):
		sm.startup_scenes_spawned.connect(host_ready.emit)


# Mirror of [method _finalize_session]. Releases the session so session-scoped
# subscribers unwind and a same-tree re-host starts clean. The embedded Server
# sibling spun up by _host_player_logic is freed here too.
func _teardown_session() -> void:
	session_ended.emit()
	_roster.clear()
	role = Role.NONE

	var parent := get_parent()
	if parent:
		var server := parent.get_node_or_null("Server") as MultiplayerTree
		if server and server != self:
			server.queue_free.call_deferred()


func _bind_api_signals(target: SceneMultiplayer) -> void:
	if not target:
		return
	_auth.bind_api(target)
	if not target.peer_connected.is_connected(_on_peer_connected):
		target.peer_connected.connect(_on_peer_connected)
	if not target.peer_disconnected.is_connected(_on_peer_disconnected):
		target.peer_disconnected.connect(_on_peer_disconnected)
	if not target.connected_to_server.is_connected(_on_connected_to_server):
		target.connected_to_server.connect(_on_connected_to_server)
	if not target.server_disconnected.is_connected(_on_server_disconnected):
		target.server_disconnected.connect(_on_server_disconnected)


func _unbind_api_signals(target: SceneMultiplayer) -> void:
	if not target:
		return
	_auth.bind_api(null)
	if target.peer_connected.is_connected(_on_peer_connected):
		target.peer_connected.disconnect(_on_peer_connected)
	if target.peer_disconnected.is_connected(_on_peer_disconnected):
		target.peer_disconnected.disconnect(_on_peer_disconnected)
	if target.connected_to_server.is_connected(_on_connected_to_server):
		target.connected_to_server.disconnect(_on_connected_to_server)
	if target.server_disconnected.is_connected(_on_server_disconnected):
		target.server_disconnected.disconnect(_on_server_disconnected)


func _notification(what: int) -> void:
	# A tree freed through a parent (rather than its own queue_free) reaches
	# tree_exiting with is_queued_for_deletion() false, so _on_exiting treats it
	# as a reparent and leaves the peer mounted. PREDELETE is the unambiguous
	# deletion signal (it never fires on reparent), so release the peer here when
	# the queued tree_exiting path did not already run.
	if what == NOTIFICATION_PREDELETE:
		_close_peer_on_delete()


func _on_exiting() -> void:
	Netw.dbg.trace("MultiplayerTree: Exiting.")

	# When re-parenting, we only unmount the api from the previous path to
	# keep the connection alive. _enter_tree handles re-registration.
	if not is_queued_for_deletion():
		_unmount_api(false)
		return

	Netw.dbg.unregister_tree(self)
	if api and api.has_multiplayer_peer():
		api.multiplayer_peer.close()
		api.multiplayer_peer = null
	_unmount_api(true)

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
	_roster.forget_peer(peer_id)
	peer_disconnected.emit(peer_id)


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
	# ONLINE guard makes a crash arriving mid-disconnect_player a no-op.
	if state == State.ONLINE:
		_transition(State.DISCONNECTING)
		_transition(State.OFFLINE)
