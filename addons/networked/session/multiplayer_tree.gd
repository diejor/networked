@icon("res://addons/networked/assets/MultiplayerTree.svg")
@tool
class_name MultiplayerTree
extends Node

const Async := preload("res://addons/networked/utils/async.gd")

## Root node for one Networked session.
##
## [member api], [member desired_role], [member role],
## [member state], and the session service registry all
## belong to this tree. Child networked nodes resolve this owner through
## [method resolve].
## [br][br]
## How the session connects is authored on [member transport], typed, and
## [member scheme] reads back out of it. What the session connects TO is a
## [NetwConnectTarget], which stays a persisted dictionary because it is written
## by a lobby row or a saved file rather than by hand.
## [codeblock]
## var enet := NetwENetParams.new()
## enet.port = 21253
## tree.transport = enet          # tree.scheme now reads &"enet"
##
## var payload := JoinPayload.new()
## payload.username = "PlayerOne"
##
## # Start as a player host.
## var host_err := await tree.host(payload)
##
## # Or connect to a known server.
## var target := NetwConnectTarget.new()
## target.scheme = &"enet"
## target.address = "127.0.0.1"
## var join_err := NetwConnector.error_of(
##     await NetwConnector.of(tree.api).join(target, payload),
## )
## [/codeblock]

const ADOPT_CONNECT_TIMEOUT := 15.0

## The current connection state of this tree, mirrored from
## [member NetwMultiplayer.state] and [member NetwMultiplayer.role].
##
## The session machine lives on [member api]. This property forwards to it so
## the tree's readers and [signal state_changed] subscribers keep working while
## the machine reacts to peer assignment.
var state: NetwMultiplayer.SessionState:
	get:
		return api.state if api else NetwMultiplayer.SessionState.OFFLINE
	set(value):
		if api:
			# Widen to int across the seam: the machine still types this slot as
			# its own enum, and the two are mirrored value for value.
			api._session.state = int(value)

## The current role of this tree in the session, mirrored from
## [member NetwMultiplayer.state] and [member NetwMultiplayer.role].
var role: NetwMultiplayer.Role:
	get:
		return api.role if api else NetwMultiplayer.Role.NONE
	set(value):
		if api:
			# Widen to int across the seam, as with state above.
			api._session.role = int(value)

## Returns [code]true[/code] while this tree is acting as a server
## (dedicated or listen server).
var is_host: bool:
	get:
		_warn_if_role_unset()
		return role == NetwMultiplayer.Role.DEDICATED_SERVER or role == NetwMultiplayer.Role.LISTEN_SERVER

## Returns [code]true[/code] while this tree is acting as a local client
## (including listen server hosts, which are also their own client).
var is_local_client: bool:
	get:
		_warn_if_role_unset()
		return role == NetwMultiplayer.Role.CLIENT or role == NetwMultiplayer.Role.LISTEN_SERVER


func _warn_if_role_unset() -> void:
	if role == NetwMultiplayer.Role.NONE:
		Netw.dbg.warn(
			"Accessed role-dependent property before role is set. "
			+ "Connect to 'session_entered' before reading is_host/is_local_client.",
		)

## The typed inputs this tree's transport opens a connection with.
##
## A params resource and the scheme that selects its transport are one fact, so
## [member scheme] reads out of this rather than being authored beside it. That
## is what makes a tree naming one transport and configuring another
## unrepresentable, and it is why authoring is typed here while a saved
## [NetwConnectTarget] and a lobby row stay dictionaries: those are written by
## a provider this build may not have registered, and this is written by hand.
## [codeblock]
## var enet := NetwENetParams.new()
## enet.port = 21253
## tree.transport = enet          # tree.scheme now reads &"enet"
## [/codeblock]
@export var transport: NetwTransportParams:
	set(value):
		transport = value
		update_configuration_warnings()
		_register_session_config()

## The scheme [member transport] is authored for, or [code]&""[/code] when none
## is set.
##
## A read-only reflection of [member NetwTransportParams.scheme]. Set
## [member transport] to change it.
var scheme: StringName:
	get:
		return transport.scheme if transport else &""

## Optional link conditions configuration to simulate latency/loss on this tree.
##
## Registered through [member NetwSessionConfig.link_conditions], which is the
## one home the session reads when it wraps the built peer.
@export var link_conditions: NetwLinkConditions:
	set(value):
		link_conditions = value
		_register_session_config()

## On headless builds, automatically starts [method host].
@export var auto_host_headless: bool = true

## The [enum NetwMultiplayer.Role] this tree intends to play once a session starts.
##
## This member is configured intent. The live [member role] is only assigned
## after a connect method succeeds. [constant NetwMultiplayer.Role.NONE] defers the choice to
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
##     connector.join() -> client
##     host_player() -> listen
## [/codeblock]
@export var desired_role: NetwMultiplayer.Role = NetwMultiplayer.Role.LISTEN_SERVER:
	set(value):
		desired_role = value
		update_configuration_warnings()
		_register_session_config()

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

## Optional [NetwMultiplayer] implementation script for this tree.
##
## Overrides [constant NetwMultiplayer.MULTIPLAYER_SCRIPT_SETTING]. Assign it
## before adding a programmatically created tree to the [SceneTree].
@export var api_script: Script:
	set(value):
		assert(
			not is_inside_tree(),
			"MultiplayerTree.api_script is immutable after tree entry",
		)
		if api_script == value:
			return
		api_script = value
		if not Engine.is_editor_hint() and api:
			api.embedding.dispose()
			api = _make_api()

## Owned [NetwMultiplayer] mounted for this session.
##
## A [MultiplayerTree] whose installed API is not [NetwMultiplayer] is
## unrepresentable: [method _mount_api] is the only place that installs one,
## and it never changes identity for the tree's lifetime. Backends that bring
## their own transport swap [member NetwMultiplayer.inner] instead of
## replacing [member api]; see [method _adopt_api].
var api: NetwMultiplayer

## The active [MultiplayerPeer] connection.
var multiplayer_peer: MultiplayerPeer:
	get:
		return api.multiplayer_peer if api else null

var _tree_name: String = ""
var _deletion_finalized: bool = false

## Local player [NetwEntity] for this tree, or [code]null[/code].
##
## The session owns the tracking off the liveness bus. This mirrors
## [member NetwMultiplayer.local_player] and [signal local_player_changed]
## re-emits [signal NetwMultiplayer.local_player_changed].
var local_player: NetwEntity:
	get:
		return api.local_player if api else null


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
static func get_role_for(node: Node) -> NetwMultiplayer.Role:
	var mt := for_node(node)
	return mt.role if mt else NetwMultiplayer.Role.NONE


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
		var local := api.local_participant
		if local and local.current_scene:
			local.current_scene = null
		api.clear_roster()


func _get_accepted_join(peer_id: int) -> ResolvedJoin:
	return api.peer_get_accepted_join(peer_id) if api else null


## Resolves the [SpawnSlot] for [param spawner_path].
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var slot := SpawnSlot.new()

	if api:
		var scene_name := StringName(spawner_path.get_scene_name())
		var scene := api.scene_handle(api.scene_find(scene_name))
		if scene != null and scene.is_declared:
			slot._scene = scene
			# Meta channel: tree_probe.gd writes this key. Both endpoints must
			# move together if the container ever stops carrying meta.
			var container := scene.record.owner if scene.record else null
			if is_instance_valid(container) \
					and container.has_meta(&"_net_scene_token"):
				slot.token = container.get_meta(&"_net_scene_token")

	return slot


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()

	if transport == null:
		warnings.append(
			"No transport is set. Assign a NetwTransportParams resource, such "
			+ "as NetwENetParams or NetwWebSocketParams.",
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
	api.embedding.offer_bare_level(_find_bare_level())
	api.embedding.settle.call_deferred()

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
	config.transport = transport
	config.link_conditions = link_conditions
	return config


func _register_session_config() -> void:
	if api:
		api.service_install(_build_session_config())


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
	return NetwMultiplayer.make(SceneMultiplayer.new(), api_script)


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return

	# The connect deadline and the peer view ride api.poll(), and a bring-up runs
	# before any peer is assigned, so the poll is unconditional: the transport
	# read inside it already tolerates having no peer.
	if api:
		api.poll()
		api.persist_tick(dt)


# Builds the host config from this tree's exported authoring, layering the
# lobby-facing fields a caller supplied on top.
# The host config this tree's authoring describes, with anything the caller
# supplied taking precedence. A caller that names no transport gets this tree's,
# which is what lets host(payload, config) carry policy without re-authoring the
# connection.
func _build_host_config(config: NetwHostConfig = null) -> NetwHostConfig:
	var built := NetwHostConfig.new()
	built.transport = transport
	if config:
		built.server_name = config.server_name
		built.visibility = config.visibility
		if config.max_players > 0:
			built.max_players = config.max_players
		if config.transport:
			built.transport = config.transport
	return built


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
func host(join_payload: JoinPayload, config: NetwHostConfig = null) -> Error:
	assert(state == NetwMultiplayer.SessionState.OFFLINE, "Must be offline to host.")
	assert(
		desired_role != NetwMultiplayer.Role.DEDICATED_SERVER,
		"host() needs a local player; a dedicated server hosts via host(null).",
	)
	var connector := NetwConnector.of(api)
	if desired_role != NetwMultiplayer.Role.CLIENT:
		return NetwConnector.error_of(
			await connector.host(join_payload, _build_host_config(config)),
		)
	# A CLIENT tree never hosts itself: it raises a dedicated sibling and joins
	# it, so the local player is a client of a server that happens to be here.
	var server := await raise_embedded_server()
	if server == null:
		return ERR_CANT_CREATE
	var host_result := await NetwConnector.of(server.api).host(
		null,
		_build_host_config(config),
	)
	if not host_result.is_ok():
		server.queue_free.call_deferred()
	var target := NetwConnectTarget.new()
	target.scheme = scheme
	target.address = NetwConnector.of(server.api).join_address() \
	if host_result.is_ok() else ""
	if target.address.is_empty():
		target.address = "localhost"
	return NetwConnector.error_of(await connector.join(target, join_payload))


## Duplicates this tree as a dedicated server sibling and adds it beside this
## one, ready to host.
##
## Node work only: the sibling is raised, its scene-manager defaults are copied,
## and it is returned. Hosting on it and joining it are
## [NetwConnector]'s, which is what keeps one manoeuvre out of two near-identical
## copies. Returns [code]null[/code] when this tree has no parent to add to.
## [codeblock]
## var server := await tree.raise_embedded_server()
## await NetwConnector.of(server.api).host(null, config)
## await NetwConnector.of(tree.api).join(target, payload)
## [/codeblock]
func raise_embedded_server() -> MultiplayerTree:
	if get_parent() == null:
		return null
	var server := duplicate() as MultiplayerTree
	server.desired_role = NetwMultiplayer.Role.DEDICATED_SERVER
	server.name = "Server"
	server.auto_host_headless = false
	get_parent().add_child.call_deferred(server)

	var loop := Engine.get_main_loop() as SceneTree
	if loop:
		await loop.process_frame

	var client_sm := get_service(MultiplayerSceneManager)
	if client_sm:
		var server_sm := server.get_service(MultiplayerSceneManager)
		if server_sm:
			for path in client_sm.get_configured_paths():
				server_sm._configure_default(path)

	if server.api == null:
		await server.ready
	return server


func _await_adopted_client_connected() -> Error:
	if not api or not api.has_multiplayer_peer():
		return ERR_UNCONFIGURED
	if api.multiplayer_peer.get_connection_status() \
			== MultiplayerPeer.CONNECTION_CONNECTED:
		return OK

	var timer := get_tree().create_timer(ADOPT_CONNECT_TIMEOUT)
	if await Async.timeout(api.connected_to_server, timer):
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
		if desired_role == NetwMultiplayer.Role.LISTEN_SERVER \
				or desired_role == NetwMultiplayer.Role.DEDICATED_SERVER:
			await NetwConnector.of(api).host(null)
		return

	if debug_join != null and not scheme.is_empty() \
			and desired_role != NetwMultiplayer.Role.DEDICATED_SERVER \
			and OS.has_feature("debug"):
		await _debug_autoconnect()


# Restores the old init_payload_debug flow: host straight into the game from
# the editor, no ConnectBrowser. Debug-only, so release never auto-connects.
func _debug_autoconnect() -> void:
	if state != NetwMultiplayer.SessionState.OFFLINE:
		return
	var payload := debug_join.to_payload()
	Netw.dbg.info(
		"MultiplayerTree: debug auto-connect as '%s'.",
		[payload.username],
	)
	await host(payload)


# Reacts to the session machine admitting a participant, running the tree's own
# join side effects (local-player binding, spawn) after the roster is remembered.
func _on_participant_admitted(_peer_id: int) -> void:
	pass


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
	api.embedding.adopt_inner(new_inner)
	api.inner.root_path = get_path()


# Forwards a state change to the session machine on [member api], which owns the
# legal-edge table and the enter and exit hooks. The tree runs its own
# session-scoped finalize and teardown off the machine's session_entered and
# session_ended signals instead.
func _transition(next: NetwMultiplayer.SessionState) -> void:
	if api:
		api._session.transition(int(next))


# Finalizes the session once the peer is live and the role is set. Rides
# [signal NetwMultiplayer.session_entered].
func _finalize_session() -> void:
	Netw.dbg.trace("MultiplayerTree: Finalizing session.")
	Netw.dbg.debug(
		"MultiplayerTree: session app_id='%s' app_tag=0x%08x.",
		[String(app_id), _app_tag()],
	)
	Netw.dbg.finalize_tree(self)


# Mirror of [method _finalize_session]. Releases the session so session-scoped
# subscribers unwind and a same-tree re-host starts clean. The embedded Server
# sibling raised by [method raise_embedded_server] is freed here too.
func _teardown_session() -> void:
	if api:
		api.clear_roster()
	role = NetwMultiplayer.Role.NONE

	var parent := get_parent()
	if parent:
		var server := parent.get_node_or_null("Server") as MultiplayerTree
		if server and server != self:
			server.queue_free.call_deferred()


func _bind_api_signals(target: NetwMultiplayer) -> void:
	if not target:
		return
	# The session owns local_player tracking off the liveness bus. The tree only
	# re-emits its edge so consumers bound to tree.local_player_changed keep
	# their tree-facing signal.
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
	# The session machine on the api owns state, role, and the lifecycle edges.
	# The tree re-emits its edge and runs its own session-scoped finalize and
	# teardown off the machine's signals.
	if not target._session.session_entered.is_connected(_finalize_session):
		target._session.session_entered.connect(_finalize_session)
	if not target._session.session_ended.is_connected(_teardown_session):
		target._session.session_ended.connect(_teardown_session)
	# The session owns admission; the tree only runs its join side effects off
	# the machine's admission signal.
	if not target._session.participant_admitted.is_connected(_on_participant_admitted):
		target._session.participant_admitted.connect(_on_participant_admitted)


func _unbind_api_signals(target: NetwMultiplayer) -> void:
	if not target:
		return
	if target._session.participant_admitted.is_connected(_on_participant_admitted):
		target._session.participant_admitted.disconnect(_on_participant_admitted)
	if target._session.session_entered.is_connected(_finalize_session):
		target._session.session_entered.disconnect(_finalize_session)
	if target._session.session_ended.is_connected(_teardown_session):
		target._session.session_ended.disconnect(_teardown_session)
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
		if api:
			api.embedding.dispose()
	elif what == NOTIFICATION_WM_CLOSE_REQUEST and not Engine.is_editor_hint():
		# The server saves every persisted entity before quitting. Clients accept
		# the close immediately, so only a host that armed the guard defers here.
		if api:
			api.persist_shutdown()


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


func _on_peer_disconnected(peer_id: int) -> void:
	Netw.dbg.info("Peer disconnected: %d", [peer_id])
	if api:
		var participant := api.peer_get_participant(peer_id)
		if participant and participant.current_scene:
			participant.current_scene = null
		api.peer_forget(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	Netw.dbg.info("Connected to server as peer %d.", [peer_id])

	set_multiplayer_authority(peer_id, false)


func _on_server_disconnected() -> void:
	# The session machine on the api owns the crash teardown edge.
	Netw.dbg.info("Disconnected from server.")
