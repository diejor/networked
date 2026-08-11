## The scene machine for one session, owned privately by [NetwMultiplayer].
##
## The session always constructs one, so scenes answer before any
## [MultiplayerSceneManager] node exists and a manager only contributes
## [NetwSceneConfig] rows. Nothing outside the addon names this class: every
## capability it holds is reached through the flat [method
## NetwMultiplayer.scene_find] family, the [method Netw.change_scene_to_file]
## doors, or a [NetwSceneHandle], because a scene is an ordinary [NetwEntity]
## and its identity is that entity's RID rather than a name in a book here.
## [codeblock]
## # What the outside world uses instead of this class:
## Netw.configure_multiplayer_scene(self)   # declare a scene
## Netw.change_scene_to_file(self, path)    # go to one
## api.scene_spawn(recipe)                  # make one
## api.scene(&"Arena")                      # find one
## NetwEntity.of(node).scene                # facts about the one you are in
## [/codeblock]
##
## What genuinely lives here is engine state with no wrapper-tier twin: the
## container construction that gives
## [constant NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD] its
## offscreen [SubViewport], the capture policy that turns a native
## [method SceneTree.change_scene_to_file] into a server request, the request
## protocol behind [method NetwMultiplayer.scene_request], and one admission row
## per live scene. The rows are keyed by entity RID rather than by container, so
## a registration outlives whichever node currently stands in for the scene.
class_name SceneCore
extends RefCounted

const AreaReparentGuard := preload("res://addons/networked/utils/area_reparent_guard.gd")

## Emitted when the local participant's primary scene changes.
signal local_scene_changed(from: NetwSceneHandle, to: NetwSceneHandle)

## Emitted when a constructed scene enters the tree on this peer.
signal scene_spawned(scene: Node)

# Emitted after the server's startup scenes have all spawned. Private because
# both consumers are framework-internal and initial_scenes is the public face of
# the same fact.
signal _startup_scenes_spawned()

## Emitted when an active scene begins processing.
signal scene_activated(scene: Node)

## Emitted when an active scene leaves the session.
signal scene_despawned(scene: Node)

## Emitted after [param entity] moves between replicated scenes.
signal entity_moved(
		entity: NetwEntity,
		from: Node,
		to: Node,
)

## Emitted when a captured native scene change settles with [param result].
##
## A native [method SceneTree.change_scene_to_file] to a scene marked through
## [method Netw.configure_multiplayer_scene] becomes a
## [method request_change_path] the server decides. This fires on every outcome,
## so a game that presents a loading screen from
## [method NetwScriptModel.SceneMarkConfig.on_pending] tears it down here even
## when the request resolves [constant ERR_UNAUTHORIZED] or
## [constant ERR_TIMEOUT] and no scene ever arrives.
## [codeblock]
## func _init() -> void:
##     Netw.configure_multiplayer_scene(self).on_pending(_show_loading)
##
## # On the persistent HUD, not the scene being left:
## api._scenes.native_change_settled.connect(func(_result): hud.hide_loading())
## [/codeblock]
signal native_change_settled(result: Error)

## Active scenes keyed by their level name, one entry per stem.
##
## The stem is not unique. When several instances of one level are live this
## holds the most recent, and [method scenes_named] answers with all of them.
## Identity is the scene's entity RID, never this key.
var scenes: Dictionary[StringName, Node] = { }

# Every live scene in registration order, so a non-unique stem can still answer
# "all instances" while `scenes` answers "an instance".
var _live_scenes: Array[Node] = []

## The scene currently presented on this peer, or [code]null[/code].
var current_scene: Node:
	get:
		return _current_scene if is_instance_valid(_current_scene) else null

## How far an admitted scene request reaches. See
## [method NetwMultiplayer.scene_set_request_reach].
var request_reach: NetwMultiplayer.SceneReach = \
		NetwMultiplayer.SceneReach.SCENE_REACH_PARTICIPANT

## The registered custom level constructor, or an empty [Callable].
var level_spawn_function: Callable:
	get:
		var config := _config()
		return config.level_spawn_function if config else Callable()

var _api_ref: WeakRef
# The session's scene declaration, owned for the session lifetime rather than
# keyed to the registrar node, so a freed registrar neither drops it nor leaks.
var _declared_config: NetwSceneConfig
var _scene_cache: Dictionary[String, PackedScene] = { }
var _allow_single_spawn := false
var _constructor_registered := false
var _current_scene: Node
var _local_participant: NetwParticipant
# The tree-less host presentation view this interface parents under the session
# root, freed on session end. Stays null when an owning MultiplayerTree already
# created its own on its branch.
var _host_scene_view: HostSceneView
var _next_request_id: int:
	get:
		return core.next_request_id
	set(value):
		core.next_request_id = value
var _pending_request_id: int:
	get:
		return core.pending_request_id
var _pending_request: NetwPromise
var _pending_from_capture := false
# The single-owner policy door installed through set_request_handler. It wins
# the only door, because a request has exactly one verdict and several
# listeners vetoing has no defined resolution.
var _request_handler := Callable()
# Participants with a scene transition in flight. Per-participant rather than
# session-wide: with per-scene isolation two participants moving into two worlds
# concurrently is the feature, and only one participant's own transition must
# serialize.
var _change_in_flight: Dictionary[int, bool] = { }
# Admission bookkeeping the core owns, one row per live scene. The container
# carries no script, so the peer set, the client-side awareness mirror, and the
# participants admitted before their roster row landed all live here.
var _admissions: Dictionary[RID, _Admission] = { }
# The record plane: the observer routing table and the in-flight request id.
# The table is keyed by the watched scene's entity RID rather than by the
# container object, which is what lets a registration outlive whichever node
# currently stands in for the scene.
var core := NetwSceneCore.new()
# The per-peer scene-request budget. The same rule the session admits joins
# under, so a peer that cannot flood one cannot flood the other.
var _request_window := NetwRateWindow.new()

## Seconds a player request waits for a server answer before resolving
## [constant ERR_TIMEOUT].
const DEFAULT_REQUEST_DEADLINE := 10.0

## The registry id the interface registers its scene constructor under, so a
## session with no [MultiplayerSceneManager] still reconstructs scene wrappers.
const SCENE_CONSTRUCTOR_ID := &"__netw_scene__"

# The _change_in_flight key a whole-session replacement holds. Peer ids are
# never 0, so it can never collide with one participant's own transition.
const _SESSION_TRANSITION := 0


## Settles [param promise] from a settle code, which is the one place the
# server's verdict becomes an outcome. OK resolves so then() fires; anything
# else rejects so catch_error() does.
static func _settle_promise(promise: NetwPromise, code: Error) -> void:
	if code == OK:
		promise.resolve(OK)
	else:
		promise.reject(code)


# Installs the single handler deciding player scene requests.
##
## The handler receives
## [code](participant, destination, args)[/code] and returns an
## [enum @GlobalScope.Error], where [constant @GlobalScope.OK] admits. It
## is the single owner of that verdict, because several listeners disagreeing
## over one request has no defined resolution. An invalid [param handler]
## restores the mark-based default.
## [br][br][b]Server Only.[/b]
func set_request_handler(handler: Callable) -> void:
	_request_handler = handler


## Registers [param callback] for one [param event] on [param scene].
##
## Keyed by the scene's entity RID rather than by a connection to the container,
## so a registration names the scene rather than the object currently standing
## in for it. Callables whose object is gone are pruned when the edge fires.
func observe(
		scene: RID,
		event: NetwMultiplayer.SceneEvent,
		callback: Callable,
) -> void:
	core.observe(scene, event, callback)


## Reverses [method observe] for one [param callback].
func unobserve(
		scene: RID,
		event: NetwMultiplayer.SceneEvent,
		callback: Callable,
) -> void:
	core.unobserve(scene, event, callback)


# One live scene's admission state. Keyed by the scene's entity RID rather than
# by its container, so it survives whichever node stands in for the scene.
class _Admission extends RefCounted:
	var container: Node
	# Peers admitted before their participant existed, retried on join.
	var pending: Dictionary[int, bool] = { }
	# The client-side awareness mirror this peer reads its own admission from.
	var layer: NetwInterestLayer


## Options for [method move].
class MoveOpts extends RefCounted:
	## Keeps timeline history across the move.
	var preserve_history := false
	## Optional gameplay label for the move.
	var reason: StringName = &"scene_move"
	## Optional destination world position.
	var target_global_position: Variant = null


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api)
	_register_constructor(api)
	api._connect_once(api.local_participant_joined, bind_local_participant)
	api._connect_once(api._session.session_entered, _on_session_entered)
	api._connect_once(api._session.session_ended, _on_session_ended)
	api._connect_once(api._liveness.entity_live, _on_entity_live)
	if not Netw.is_test_env():
		var scene_tree := Engine.get_main_loop() as SceneTree
		if scene_tree:
			api._connect_once(scene_tree.scene_changed, _on_native_scene_changed)


## Registers the session's scene declaration. The API owns [param config] for the
## session lifetime, so a registrar node need not deregister it on the way out.
func configure(config: NetwSceneConfig) -> void:
	_declared_config = config
	_register_constructor(_api())
	refresh_current_scene.call_deferred()


## Drops the session's scene declaration.
func deconfigure() -> void:
	_declared_config = null
	refresh_current_scene.call_deferred()


## Binds the accepted local [param participant] to [member current_scene].
func bind_local_participant(participant: NetwParticipant) -> void:
	if _local_participant == participant:
		sync_local_participant.call_deferred()
		return
	_release_local_participant()
	_local_participant = participant
	if _local_participant:
		var api := _api()
		if api:
			api._connect_once(
				_local_participant.scene_changed,
				_on_local_scene_changed,
			)
	sync_local_participant.call_deferred()
	refresh_current_scene.call_deferred()


## Infers the local participant's scene from wrapper awareness.
func sync_local_participant() -> void:
	var api := _api()
	if _local_participant == null or api == null:
		return
	for scene_node: Node in scenes.values():
		if not is_instance_valid(scene_node):
			continue
		var layer := api._interest.get_layer(
			api._scene_layer_id(api.rid_of(scene_node)),
		)
		var entity := NetwEntity.of(scene_node)
		if layer and entity and layer.has_entity(entity):
			_local_participant.current_scene = _handle_for(scene_node)
			return


## Recomputes [member current_scene] from the local presentation state.
func refresh_current_scene() -> void:
	_current_scene = _resolve_current_scene()


## Releases SceneTree and participant signal connections.
func dispose() -> void:
	_release_local_participant()
	_clear_request_state()
	var api := _api()
	if api:
		if api.local_participant_joined.is_connected(bind_local_participant):
			api.local_participant_joined.disconnect(bind_local_participant)
		if api._session.session_entered.is_connected(_on_session_entered):
			api._session.session_entered.disconnect(_on_session_entered)
		if api._session.session_ended.is_connected(_on_session_ended):
			api._session.session_ended.disconnect(_on_session_ended)
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree and scene_tree.scene_changed.is_connected(
		_on_native_scene_changed,
	):
		scene_tree.scene_changed.disconnect(_on_native_scene_changed)
	_declared_config = null
	_scene_cache.clear()
	scenes.clear()


## Whether a session declared its scenes through a registered [NetwSceneConfig].
## A caller distinguishes a configured session from a bare one before the
## session goes online, so an auto-declaration for a dropped-in level defers to
## an explicit declaration.
func has_declaration() -> bool:
	return _config() != null


## Returns the active scene named [param scene_name], or [code]null[/code].
##
## Answers in the wrapper tier: a caller that looked a scene up wants to act on
## it, and the container node is an implementation detail of how it is mounted.
func scene(scene_name: StringName) -> NetwSceneHandle:
	var record := NetwEntity.of(_container(scene_name))
	return record.scene if record else null


# The container node standing in for [param scene_name], for the paths inside
# this core that mount, free, and reparent it.
func _container(scene_name: StringName) -> Node:
	var active := scenes.get(scene_name) as Node
	return active if is_instance_valid(active) else null


## Returns every live scene whose stem is [param scene_name].
##
## Stems collide freely, so this is the honest lookup when a session runs
## several instances of one level. [method scene] answers with one of them.
func scenes_named(scene_name: StringName) -> Array[Node]:
	var out: Array[Node] = []
	for candidate: Node in _live_scenes:
		var content := _level_of(candidate)
		if content == null:
			continue
		if StringName(content.name) == scene_name:
			out.append(candidate)
	return out


## Returns every live scene in the session.
func live_scenes() -> Array[Node]:
	var out: Array[Node] = []
	for candidate: Node in _live_scenes:
		if is_instance_valid(candidate):
			out.append(candidate)
	return out


## Returns the [Node] containing [param node], or [code]null[/code].
func scene_of(node: Node) -> Node:
	if not is_instance_valid(node):
		return null
	var current := node
	while current:
		if _live_scenes.has(current):
			return current
		current = current.get_parent()
	return null


## Activates a declared scene name or a file-backed [PackedScene].
##
## A [PackedScene] does not need to be declared in advance. Returns the active
## wrapper, or [code]null[/code] when activation fails.
## [br][br][b]Server Only.[/b]
func activate(scene_ref: Variant) -> Node:
	var api := _api()
	assert(api and api.is_server(), "Scene activation is server-only.")
	if scene_ref is PackedScene:
		var packed := scene_ref as PackedScene
		if packed.resource_path.is_empty():
			push_error("Cannot activate an in-memory PackedScene.")
			return null
		var scene_name := StringName(
			packed.resource_path.get_file().get_basename(),
		)
		var active := _container(scene_name)
		if active:
			_level_of(active).process_mode = Node.PROCESS_MODE_INHERIT
			scene_activated.emit(active)
			return active
		var spawned := spawn(packed.resource_path)
		if spawned:
			scene_activated.emit(spawned)
		return spawned
	if scene_ref is String or scene_ref is StringName:
		return activate_scene(StringName(scene_ref))
	push_error("Scene activation expects a scene name or PackedScene.")
	return null


## Ensures the declared scene named [param scene_name] is active and forces its
## level to process. Returns the active wrapper, or [code]null[/code] on failure.
## [br][br][b]Server Only.[/b]
func activate_scene(scene_name: StringName) -> Node:
	var api := _api()
	assert(api and api.is_server(), "Scene activation is server-only.")
	if _container(scene_name) == null:
		if level_spawn_function.is_valid():
			spawn(_spawn_data().get(scene_name, scene_name))
		else:
			spawn_scene(scene_name)
	var active := _container(scene_name)
	if active == null:
		Netw.dbg.error(
			"Failed to activate scene '%s'.",
			[scene_name],
			func(m): push_error(m),
		)
		return null
	_level_of(active).process_mode = Node.PROCESS_MODE_INHERIT
	scene_activated.emit(active)
	return active


## Constructs and replicates the declared scene named [param scene_name].
## [br][br][b]Server Only.[/b]
func spawn_scene(scene_name: StringName) -> void:
	if _container(scene_name):
		return
	if not _can_spawn_scene(scene_name):
		return
	var path := _scene_path_for(scene_name)
	if path.is_empty():
		Netw.dbg.error(
			"Cannot spawn scene '%s': not declared.",
			[scene_name],
			func(m): push_error(m),
		)
		return
	spawn(path)


## Disables processing for the active scene named [param scene_name].
## [br][br][b]Server Only.[/b]
func freeze(scene_name: StringName) -> void:
	var api := _api()
	assert(api and api.is_server(), "Scene freezing is server-only.")
	var active := _container(scene_name)
	if active:
		_level_of(active).process_mode = Node.PROCESS_MODE_DISABLED


## Removes the active scene named [param scene_name] from the session.
## [br][br][b]Server Only.[/b]
func destroy(scene_name: StringName) -> void:
	var api := _api()
	assert(api and api.is_server(), "Scene destruction is server-only.")
	var active := _container(scene_name)
	if active == null:
		return
	if active.get_parent():
		active.get_parent().remove_child(active)
	active.queue_free()


## Removes [param scene_name] from the active registry now, then frees the
## wrapper after [param drain_frames] process frames.
##
## Teardown paths that must end game logic immediately while keeping stale
## replication paths resolvable for a short drain window use this over
## [method destroy].
## [br][br][b]Server Only.[/b]
func retire(scene_name: StringName, drain_frames: int = 8) -> void:
	var api := _api()
	assert(api and api.is_server(), "Scene retirement is server-only.")
	var active := _container(scene_name)
	if active == null:
		return
	_remove_scene(active)
	_free_retired_scene.call_deferred(active, maxi(0, drain_frames))


## Returns the [Node] a freshly instantiated [param player] enters,
## honoring a stored teleport scene over [param fallback]. Awaits spawn
## hydration so persisted spawn state rides the SPAWN frame.
## [br][br][b]Server Only.[/b]
func resolve_hydrated_spawn_scene(
		player: Node,
		fallback: Node,
) -> Node:
	var api := _api()
	var entity := NetwEntity.of(player)
	if api and entity:
		var engine := api._persistence.engine_for(entity)
		if engine and engine.wants_spawn_hydration():
			await engine.hydrate()
	var tp: TPComponent = player.get_node_or_null("%TPComponent")
	if not tp or tp.current_scene_name.is_empty():
		return fallback
	# Persistence stores the stem, not an identity, so restoring means "put this
	# player into an instance of this stem". With several instances live any of
	# them satisfies the save, which is the semantic a non-unique stem carries.
	var scene_name := StringName(tp.current_scene_name)
	if _container(scene_name) == null:
		await activate_scene(scene_name)
	var active := _container(scene_name)
	return active if active else fallback


## Moves [param entity] into [param destination].
##
## [param destination] accepts an active [Node], a declared scene
## name, or a file-backed [PackedScene]. The returned promise resolves after
## both physics flushes, replicated reparenting, participant membership, and
## persistence have settled. The move rides [method NetwEntity.reparent_to], so
## a following camera resets its smoothing on [signal NetwEntity.reparented]
## when [param opts] carries a destination position.
## [br][br][b]Server Only.[/b]
func move(
		entity: NetwEntity,
		destination: Variant,
		opts: MoveOpts = null,
) -> NetwPromise:
	var api := _api()
	assert(api and api.is_server(), "Scene movement is server-only.")
	var promise := NetwPromise.new()
	_move_entity(entity, destination, opts, promise)
	return promise


## Replaces the active SINGLE scene and moves every participant into it.
##
## [param destination] accepts the same values as [method move].
## [br][br][b]Server Only.[/b]
func change_to(destination: Variant) -> NetwPromise:
	var api := _api()
	assert(api and api.is_server(), "Scene changes are server-only.")
	var promise := NetwPromise.new()
	_change_single_scene(destination, promise)
	return promise


## The muscle-memory front door mirroring [method SceneTree.change_scene_to_file].
##
## [param requester] carries the session and the local mover, so server
## authority runs the change directly while a client turns it into a
## [method request_change_path] the server decides. Both resolve to the same
## verbs, so the front door and the captured native change never diverge.
## [codeblock]
## var promise := Netw.change_scene_to_file(self, "res://arena.tscn")
## await promise.settled
## if promise.code != OK:
##     status.text = "Could not change scene."
## [/codeblock]
func change_scene_to_file(
		requester: Node,
		path: String,
) -> NetwPromise:
	return _front_door_change(requester, ResourceUID.ensure_path(path))


## The front door for a file-backed [PackedScene], mirroring
## [method SceneTree.change_scene_to_packed]. See [method change_scene_to_file].
func change_scene_to_packed(
		requester: Node,
		packed: PackedScene,
) -> NetwPromise:
	if packed == null or packed.resource_path.is_empty():
		push_error(
			"change_scene_to_packed needs a file-backed PackedScene so a "
			+ "client can request it by path.",
		)
		return _resolved_promise(ERR_UNAVAILABLE)
	return _front_door_change(requester, packed.resource_path)


## Re-enters the scene this peer currently presents, mirroring
## [method SceneTree.reload_current_scene]. See [method change_scene_to_file].
func reload_current_scene(requester: Node) -> NetwPromise:
	var here := current_scene
	if here == null or _level_of(here) == null:
		push_error("reload_current_scene: this peer presents no scene.")
		return _resolved_promise(ERR_UNAVAILABLE)
	var path := _level_of(here).scene_file_path
	if path.is_empty():
		push_error("reload_current_scene: the current scene has no file path.")
		return _resolved_promise(ERR_UNAVAILABLE)
	return _front_door_change(requester, ResourceUID.ensure_path(path))


# Routes a front-door change to authority's verb or a client's request. Server
# authority applies the change directly at the configured reach; a client
# asks through the same request path the server policy decides.
func _front_door_change(requester: Node, path: String) -> NetwPromise:
	var api := _api()
	if api == null or path.is_empty():
		return _resolved_promise(ERR_UNAVAILABLE)
	if not api.is_server():
		return request_change_path(path)
	var packed := load(path) as PackedScene
	if packed == null:
		return _resolved_promise(ERR_UNAVAILABLE)
	return _apply_player_change(_requester_participant(requester), packed)


# The participant a front-door call moves: the one owning the requester node, or
# the local participant when the requester is not itself a player entity.
func _requester_participant(requester: Node) -> NetwParticipant:
	var api := _api()
	if is_instance_valid(requester):
		var entity := NetwEntity.of(requester)
		if entity and entity.participant:
			return entity.participant
	return api.local_participant if api else null


# A promise already carrying [param result].
func _resolved_promise(result: Error) -> NetwPromise:
	var promise := NetwPromise.new()
	promise.resolve(result)
	return promise


## Requests that server authority move the local player to [param scene_name].
##
## A newer request resolves the previous local promise as
## [constant ERR_SKIP]. A declared scene admits by
## default unless the installed request handler refuses it. A request
## unanswered within [constant DEFAULT_REQUEST_DEADLINE] resolves
## [constant ERR_TIMEOUT].
## [br][br][b]Player request.[/b]
func request_change(
		scene_name: StringName,
		args: Array = [],
) -> NetwPromise:
	return _start_request(false, scene_name, args)


## Requests that server authority move the local player to the scene at
## [param scene_path].
##
## The path variant carries a [code]res://[/code] or [code]uid://[/code]
## reference the server resolves through its change policy, rather than a
## declared name. Otherwise identical to [method request_change].
## [br][br][b]Player request.[/b]
func request_change_path(
		scene_path: String,
		args: Array = [],
) -> NetwPromise:
	return _start_request(true, scene_path, args)


# Supersedes any pending request, sends the frame, and arms the deadline.
func _start_request(
		is_path: bool,
		scene_ref: Variant,
		args: Array,
		deadline: float = DEFAULT_REQUEST_DEADLINE,
		from_capture: bool = false,
) -> NetwPromise:
	if _pending_request and not _pending_request.is_settled:
		_pending_request.reject(ERR_SKIP)
		_settle_native_change(ERR_SKIP)
	_pending_from_capture = from_capture
	var promise := NetwPromise.new()
	var api := _api()
	if api == null:
		promise.reject(ERR_UNAVAILABLE)
		return promise
	_pending_request = promise
	core.open_request()
	api._replication.send_to(
		1,
		0,
		NetwFrameEnvelope.Channel.SESSION_SCENE_REQUEST,
		var_to_bytes([_pending_request_id, is_path, scene_ref, args]),
		true,
	)
	_arm_request_deadline(_pending_request_id, deadline)
	return promise


# Resolves a still-pending request as TIMED_OUT once its deadline elapses.
func _arm_request_deadline(request_id: int, deadline: float) -> void:
	if deadline <= 0.0:
		return
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree == null:
		return
	var timer := scene_tree.create_timer(deadline)
	var api := _api()
	if api:
		api._connect_once(timer.timeout, _on_request_deadline.bind(request_id))


func _on_request_deadline(request_id: int) -> void:
	if not core.is_current(request_id) or _pending_request == null:
		return
	_pending_request.reject(ERR_TIMEOUT)
	_pending_request = null
	core.close_request()
	_settle_native_change(ERR_TIMEOUT)


# Converts a marked scene's native tree entry into the role's replicated verb.
# Called by the detach hook only for a live session and a non-framework entry.
func _handle_native_scene_entry(node: Node) -> void:
	var api := _api()
	if api == null:
		return
	var path := node.scene_file_path
	if path.is_empty():
		# An in-memory instance has no path to request, mirroring activate's
		# rejection of a pathless PackedScene.
		push_error(
			"A marked scene entered natively has no resource_path, so it "
			+ "cannot become a server request. Instantiate it from a saved "
			+ "scene file.",
		)
		return
	if api.is_server():
		_discard_and_respawn(node, path)
	else:
		_detach_and_request(node, path)


# Server: the native instance already ran _ready outside the wrapper, gate, and
# hydration, so it is discarded and re-spawned authoritatively through the
# pipeline, the same double-instantiation bare-level adoption already pays. A
# listen host under CONCURRENT reads the bare call as "move me", the same meaning
# a client's bare call carries, so the host relocates rather than spawning a
# world it does not enter.
func _discard_and_respawn(node: Node, path: String) -> void:
	_hide_and_free(node)
	var packed := load(ResourceUID.ensure_path(path)) as PackedScene
	if packed == null:
		return
	if request_reach == NetwMultiplayer.SceneReach.SCENE_REACH_SESSION \
			and _has_active_scene():
		change_to(packed)
	elif _is_concurrent_host_move_me():
		_apply_player_change(_api().local_participant, packed)
	else:
		activate(packed)


# Whether a bare native change on this peer means "move the local player": a
# listen host presenting concurrent worlds, with a local participant to move.
func _is_concurrent_host_move_me() -> bool:
	var api := _api()
	if api == null or api.role != NetwMultiplayer.Role.LISTEN_SERVER \
			or api.local_participant == null:
		return false
	var here := api.local_participant.current_scene
	return here != null and here.isolation \
			== NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD


# Whether any scene is currently active.
func _has_active_scene() -> bool:
	for active: Node in scenes.values():
		if is_instance_valid(active):
			return true
	return false


# Client: detach the local instance and wait for the authoritative scene to
# arrive as spawn frames, the byte-identical state a freshly admitted client is
# already in.
func _detach_and_request(node: Node, path: String) -> void:
	var config := NetwScriptModel.get_scene_config(node.get_script())
	_invoke_pending_hook(node, config)
	_hide_and_free(node)
	var deadline := config.deadline if config and config.deadline > 0.0 \
	else DEFAULT_REQUEST_DEADLINE
	_start_request(true, path, [], deadline, true)


# Runs the marked scene's on_pending hook, a side-effect callback for the game to
# present its own loading UI. Called on the native instance before it is freed.
# The game tears its UI down on [signal native_change_settled], since a denied or
# timed-out request never reaches the scene that would clear it.
func _invoke_pending_hook(
		node: Node,
		config: NetwScriptModel.SceneMarkConfig,
) -> void:
	if config == null or config.pending_method.is_empty():
		return
	if node.has_method(config.pending_method):
		node.call(config.pending_method)


# Emits the capture-completion signal for a request that began as a native
# change, so the game undoes an on_pending loading screen on any outcome.
func _settle_native_change(result: Error) -> void:
	if _pending_from_capture:
		native_change_settled.emit(result)
	_pending_from_capture = false


# Hides and frees a detached native instance. The free defers so the engine
# finishes assigning current_scene before the node leaves the tree.
func _hide_and_free(node: Node) -> void:
	node.process_mode = Node.PROCESS_MODE_DISABLED
	if node is CanvasItem or node is Node3D:
		node.set(&"visible", false)
	node.queue_free()


# Registers the interface's host-less scene constructor once per session. The
# args config is script-keyed and shared across sessions; the registry entry is
# per-pipeline, so both bind to this api's replication.
func _register_constructor(api: NetwMultiplayer) -> void:
	if _constructor_registered or api == null:
		return
	Netw.configure_spawn(_spawn_scene_node)
	api._replication.register_spawn_constructor(
		SCENE_CONSTRUCTOR_ID,
		_spawn_scene_node,
	)
	_constructor_registered = true


# Server startup spawns the declared initial scenes, deferred one idle frame so
# the session has settled, then announces completion.
func _on_session_entered() -> void:
	var api := _api()
	if api and api.is_server():
		_spawn_initial_scenes.call_deferred()
	_ensure_host_scene_view.call_deferred()


# Turns one direct packed level a scoped embedding offered into the default
# SINGLE scene declaration, run once from the settle step so the adopted
# the declaration is registered before the host view and startup spawns read it. Skipped
# when an explicit declaration already won or a manager already authored the
# session, and a no-op under a root install, which offers no bare level.
func _adopt_bare_level(level: Node) -> void:
	if not is_instance_valid(level):
		return
	if has_declaration():
		return
	var api := _api()
	if api == null:
		return
	var root := api.root
	if root == null:
		return
	for child in root.get_children():
		if child is MultiplayerSceneManager:
			return
	var path := ResourceUID.ensure_path(level.scene_file_path)
	var parent := level.get_parent()
	if parent:
		parent.remove_child(level)
	level.free()
	var manager := MultiplayerSceneManager.new()
	manager.name = &"MultiplayerSceneManager"
	manager._configure_default(path)
	root.add_child(manager)


# A CONCURRENT listen-server host isolates its own world in an offscreen
# SubViewport that only a [HostSceneView] draws. It runs once from the settle
# step, keyed on the authored [member NetwSessionConfig.desired_role], and
# again when the session enters online, keyed on the resolved
# [member NetwMultiplayer.role], parenting the view under
# [member NetwMultiplayer.root] so it survives a native scene change alongside the
# session content. Idempotent, and skipped when a view already owns the display.
func _ensure_host_scene_view() -> void:
	var api := _api()
	if api == null:
		return
	if api.role != NetwMultiplayer.Role.LISTEN_SERVER \
			and api._session.desired_role != NetwMultiplayer.Role.LISTEN_SERVER:
		return
	var root := api.root
	if root == null or not _hosts_an_isolated_world():
		return
	for child in root.get_children():
		if child is HostSceneView:
			return
	var view := HostSceneView.new()
	view.name = &"HostSceneView"
	_host_scene_view = view
	root.add_child(view)


# Whether a live scene owns its own world, which is the only thing a host view
# has to draw. A host whose scenes all share the session's world renders like a
# client and needs none.
func _hosts_an_isolated_world() -> bool:
	for active: Node in _live_scenes:
		if active is SubViewport:
			return true
	return false


func _spawn_initial_scenes() -> void:
	var config := _config()
	if config:
		for packed: PackedScene in config.initial_scenes:
			if packed and not packed.resource_path.is_empty():
				if _packed_scene_active(packed):
					continue
				spawn(packed.resource_path)
	_startup_scenes_spawned.emit()


# Whether a scene with [param packed]'s root name is already active, so a startup
# spawn never duplicates a scene a join already brought online.
func _packed_scene_active(packed: PackedScene) -> bool:
	var state := packed.get_state()
	if state.get_node_count() == 0:
		return false
	return _container(StringName(state.get_node_name(0))) != null


## Constructs and replicates a scene wrapper from [param data], parenting it
## under the session anchor.
##
## [param data] is a resource path unless [member NetwSceneConfig.level_spawn_function]
## supplies a custom constructor, which receives [param data] verbatim on every
## peer. The host-less registry recipe reconstructs the wrapper with no manager
## node. Returns the active wrapper, or [code]null[/code] when the spawn fails.
##
## [param isolation] declares whether this one scene hosts its own world, as an
## [enum NetwMultiplayer.SceneIsolation]. It rides the recipe, so two scenes in
## one session may differ. Passing [code]-1[/code] takes the session default.
## [br][br][b]Server Only.[/b]
func spawn(data: Variant, isolation: int = -1) -> Node:
	var api := _api()
	assert(api and api.is_server(), "Scene spawning is server-only.")
	if not _can_spawn_scene(&""):
		return null
	var scene_node := api._replication.spawn_registered(
		SCENE_CONSTRUCTOR_ID,
		[data, _default_isolation() if isolation < 0 else isolation],
	) as Node
	if scene_node:
		var parent := _scene_parent()
		if parent:
			parent.add_child(scene_node)
	return scene_node


# The isolation a scene takes when its recipe names none. Sharing the session's
# world is the neutral answer: a scene that needs its own says so.
func _default_isolation() -> NetwMultiplayer.SceneIsolation:
	var config := _config()
	return config.isolation if config \
	else NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE


# The registered scene constructor, run on every peer to build an identical
# wrapper from the activation data and the isolation the spawn declared. The
# isolation rides the recipe rather than session state so two scenes in one
# session can differ, and _encode_fn_args keeps the wire in sync from this
# signature alone.
func _spawn_scene_node(data: Variant, isolation: int) -> Node:
	var api := _api()
	var hosting := api.is_server() if api else false
	var level: Node
	if level_spawn_function.is_valid():
		level = level_spawn_function.call(data)
		if not is_instance_valid(level):
			Netw.dbg.error(
				"scene spawn function returned null.",
				func(m): push_error(m),
			)
			return null
	elif data is String:
		var path: String = data
		var packed: PackedScene
		if _scene_cache.has(path):
			packed = _scene_cache[path]
			_scene_cache.erase(path)
		else:
			packed = load(path)
		level = packed.instantiate()
	else:
		Netw.dbg.error("invalid scene spawn data.", func(m): push_error(m))
		return null
	var wrapper := _make_scene_wrapper(hosting, isolation)
	_install_level(wrapper, level)
	if api:
		api._connect_once(wrapper.tree_entered, _on_scene_entered.bind(wrapper))
		api._connect_once(wrapper.tree_exited, _on_scene_exited.bind(wrapper))
	return wrapper


# Builds the container the scene's declared isolation selects. A hosting peer
# isolates an OWN_WORLD scene in a [SubViewport] with its own world; every other
# case is a plain [Node], because only the host simulates. Neither carries a
# script: a scene is its entity record, not a node class.
func _make_scene_wrapper(hosting: bool, isolation: int) -> Node:
	var wrapper: Node
	if isolation == NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD \
			and hosting:
		var viewport := SubViewport.new()
		viewport.own_world_3d = true
		viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		wrapper = viewport
	else:
		wrapper = Node.new()
	wrapper.name = &"Scene"
	wrapper.set_meta(NetwSceneHandle._SCENE_META, true)
	return wrapper


# The node every scene wrapper parents under. A declaration node co-locates its
# scenes so tree order and process order match every peer, and the session root
# anchors them when no manager node exists. Both resolve as spawn anchors
# because the manager is a descendant of the root.
func _scene_parent() -> Node:
	var api := _api()
	if api == null:
		return null
	var manager_node := api.get_service(MultiplayerSceneManager)
	if is_instance_valid(manager_node):
		return manager_node
	return api.root


# Warns when a second scene joins a session whose scenes all share one world,
# which is legal but usually a mistake: two levels in one physics space collide.
# A scene that declares SCENE_ISOLATION_OWN_WORLD says it meant it.
func _can_spawn_scene(scene_name: StringName) -> bool:
	if _allow_single_spawn or not _has_active_scene():
		return true
	for active: Node in _live_scenes:
		var record := NetwEntity.of(active)
		if record != null and record.scene_isolation \
				== NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD:
			return true
	var detail := "another scene"
	if not scene_name.is_empty():
		detail = "scene '%s'" % scene_name
	Netw.dbg.warn(
		"Activating %s while a shared-world scene is active. Both levels "
		+ "share one physics space. Declare isolation with "
		+ "Netw.configure_multiplayer_scene(self).isolated() if that is wrong.",
		[detail],
	)
	return true


# The declared resource path for a scene name, or an empty string.
func _scene_path_for(scene_name: StringName) -> String:
	var config := _config()
	if config:
		var packed := config.scenes.get(scene_name) as PackedScene
		if packed and not packed.resource_path.is_empty():
			return packed.resource_path
	return ""


# The declared spawn data keyed by scene name.
func _spawn_data() -> Dictionary:
	var config := _config()
	return config.spawn_data if config else { }


func _on_scene_entered(scene_node: Node) -> void:
	var content := _level_of(scene_node)
	if content == null:
		return
	var scene_name := StringName(content.name)
	# The stem is a non-unique label: N instances of one level are N scenes,
	# each owning its own route-keyed admission boundary. This map answers
	# "an instance of this stem", _live_scenes answers "every instance".
	scenes[scene_name] = scene_node
	if not _live_scenes.has(scene_node):
		_live_scenes.append(scene_node)
	_open_admission(scene_node)
	_ensure_host_scene_view()
	scene_spawned.emit(scene_node)
	var live_api := _api()
	var record := NetwEntity.of(scene_node)
	if live_api and record:
		live_api.scene_live.emit(record.scene)
	sync_local_participant.call_deferred()
	refresh_current_scene.call_deferred()


func _on_scene_exited(scene_node: Node) -> void:
	_close_admission(scene_node)
	_remove_scene(scene_node)
	scene_despawned.emit(scene_node)

#region Admission

# Opens one scene's admission row. A server reads its own admission edges. A
# client has none to read, so it learns membership from awareness of the scene
# entity itself, which is the same fact arriving the only way a client can see
# it.
func _open_admission(scene_node: Node) -> void:
	var api := _api()
	if api == null:
		return
	var scene := api.rid_of(scene_node)
	if not scene.is_valid() or _admissions.has(scene):
		return
	var row := _Admission.new()
	row.container = scene_node
	_admissions[scene] = row
	if api.is_server():
		for peer: int in api.scene_get_peers(scene):
			_report_participant(scene, peer, true)
		return
	api._connect_once(api.participant_joined, _on_participant_joined)
	var boundary := api._interest.get_layer(api._scene_layer_id(scene))
	if boundary == null:
		return
	row.layer = boundary
	api._connect_once(boundary.entity_visible, _on_scene_visible.bind(scene))
	api._connect_once(boundary.entity_hidden, _on_scene_hidden.bind(scene))
	var record := NetwEntity.of(scene_node)
	if record and boundary.has_entity(record):
		_report_local_participant(scene, true)


# Closes one scene's admission row, clearing the membership of everyone still
# recorded as being in it.
func _close_admission(scene_node: Node) -> void:
	var api := _api()
	if api == null:
		return
	var scene := api.rid_of(scene_node)
	var row: _Admission = _admissions.get(scene)
	if row == null:
		return
	if row.layer:
		var visible := _on_scene_visible.bind(scene)
		if row.layer.entity_visible.is_connected(visible):
			row.layer.entity_visible.disconnect(visible)
		var hidden := _on_scene_hidden.bind(scene)
		if row.layer.entity_hidden.is_connected(hidden):
			row.layer.entity_hidden.disconnect(hidden)
	_admissions.erase(scene)
	if api.local_participant:
		_clear_membership(api.local_participant, scene)
	for peer: int in api.scene_get_peers(scene):
		var participant := api.peer_get_participant(peer)
		if participant:
			_clear_membership(participant, scene)


## Admits [param peer] to [param scene], reporting the participant edge when the
## peer was not already admitted.
## [br][br][b]Server Only.[/b]
func admit_peer(scene: RID, peer: int) -> void:
	var api := _api()
	if api == null or peer == 0:
		return
	# Admission arms the boundary. A scene activated this frame carries no layer
	# until the interest pass syncs membership into it, so a lookup that cannot
	# create one silently drops every admission a scene change makes.
	var boundary := api._interest.layer_for(api._scene_layer_id(scene))
	if boundary == null:
		return
	var was_admitted := boundary.viewers.has(peer)
	boundary.add_viewer(peer)
	if not was_admitted and boundary.viewers.has(peer):
		_report_participant(scene, peer, true)


## Releases [param peer] from [param scene], reporting the participant edge when
## the peer was admitted.
## [br][br][b]Server Only.[/b]
func release_peer(scene: RID, peer: int) -> void:
	var api := _api()
	if api == null:
		return
	var boundary := api._interest.get_layer(api._scene_layer_id(scene))
	if boundary == null:
		return
	var was_admitted := boundary.viewers.has(peer)
	boundary.remove_viewer(peer)
	if was_admitted and not boundary.viewers.has(peer):
		_report_participant(scene, peer, false)


# Records one participant's arrival or departure and reports it to observers. A
# peer admitted before its roster row lands is parked and retried on join.
func _report_participant(scene: RID, peer: int, present: bool) -> void:
	var api := _api()
	var row: _Admission = _admissions.get(scene)
	if api == null:
		return
	var participant := api.peer_get_participant(peer)
	if participant == null:
		if present and row:
			row.pending[peer] = true
		return
	if row:
		row.pending.erase(peer)
	if present:
		participant.current_scene = api.scene_handle(scene)
	else:
		_clear_membership_deferred(participant, scene)
	_dispatch_observers(
		scene,
		NetwMultiplayer.SceneEvent.SCENE_EVENT_PARTICIPANT,
		present,
		peer,
	)


# Clears membership only if [param scene] is still where the participant is, so
# a move that reassigns membership first wins over a late release.
func _clear_membership(participant: NetwParticipant, scene: RID) -> void:
	var current := participant.current_scene
	if current != null and current.entity == scene:
		participant.current_scene = null


func _clear_membership_deferred(
		participant: NetwParticipant,
		scene: RID,
) -> void:
	_clear_membership.call_deferred(participant, scene)


func _on_scene_visible(entity: NetwEntity, scene: RID) -> void:
	var api := _api()
	if api and entity == api._entity_wrapper(scene):
		_report_local_participant(scene, true)


func _on_scene_hidden(entity: NetwEntity, scene: RID) -> void:
	var api := _api()
	if api and entity == api._entity_wrapper(scene):
		_report_local_participant(scene, false)


func _report_local_participant(scene: RID, present: bool) -> void:
	var api := _api()
	if api and api.local_participant:
		_report_participant(scene, api.local_participant.peer_id, present)


# Retries the admission fact for a peer whose roster row landed after it was
# already admitted.
func _on_participant_joined(participant: NetwParticipant) -> void:
	var api := _api()
	if api == null:
		return
	for scene: RID in _admissions.keys():
		var row: _Admission = _admissions[scene]
		if row.pending.has(participant.peer_id):
			_report_participant(scene, participant.peer_id, true)
			continue
		if row.layer == null or participant != api.local_participant:
			continue
		var record := api._entity_wrapper(scene)
		if record and row.layer.has_entity(record):
			_report_participant(scene, participant.peer_id, true)


# Tells [param peer] on its own side that it no longer belongs to [param scene].
# A client learns membership from awareness of the scene entity, which cannot
# report a release on its own, so the release is sent.
func _notify_scene_released(scene: RID, peer: int) -> void:
	var api := _api()
	var participant := api.peer_get_participant(peer) if api else null
	if participant == null:
		return
	var current := participant.current_scene
	if current == null or current.entity != scene:
		return
	var payload := var_to_bytes(api._scene_layer_id(scene))
	if peer == api.get_unique_id():
		_handle_scene_released_frame(payload, 1)
	else:
		api._replication.send_to(
			peer,
			0,
			NetwFrameEnvelope.Channel.SESSION_SCENE_RELEASED,
			payload,
			true,
		)

#endregion

# Watches one entity's tree edges for as long as it is live. The scene is
# resolved at the moment of each edge rather than remembered, so a reparent
# across scenes reports a leave against the old scene, which tree_exiting still
# sees, and an enter against the new one, with nothing handing the entity over.
func _on_entity_live(_route: int, entity: NetwEntity) -> void:
	watch_entity(entity)


## Reports [param entity]'s scene edges for as long as it lives.
##
## The liveness bus covers every routed entity. This is also the door for one
## that never routes, such as a player seated directly into a scene, whose
## admission would otherwise outlive it.
func watch_entity(entity: NetwEntity) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	var api := _api()
	if api == null:
		return
	var node := entity.owner
	# The id is resolved once, here, while the entity is alive. Re-resolving per
	# edge would mint a fresh one for an entity on its way out, because a despawn
	# clears the record's id before the node leaves the tree.
	var subject := api.rid_of(node)
	if not subject.is_valid():
		return
	var entered := _report_entity_edge.bind(entity, subject, true)
	var exited := _report_entity_edge.bind(entity, subject, false)
	if not node.tree_entered.is_connected(entered):
		node.tree_entered.connect(entered)
	if not node.tree_exiting.is_connected(exited):
		node.tree_exiting.connect(exited)
	if node.is_inside_tree():
		_report_entity_edge(entity, subject, true)
	# A player's admission is released against the scene it was seated into.
	# Teardown cannot resolve that scene, because the node is already leaving the
	# tree the walk would follow, so the seat is remembered here instead.
	if entity.peer_id != 0:
		var seat := api.scene_of(subject)
		if seat.is_valid() and seat != subject:
			var leaving := _on_player_exiting.bind(seat, entity, entity.peer_id)
			if not node.tree_exiting.is_connected(leaving):
				node.tree_exiting.connect(leaving)


# Defers the release so it can tell a free from a reparent, which the exit
# signal itself cannot.
func _on_player_exiting(seat: RID, entity: NetwEntity, peer: int) -> void:
	_release_departed_player.call_deferred(seat, entity, peer)


# Reports one entity crossing a scene boundary. A player reports on both the
# entity and the player edge, because a player is an entity that carries a peer
# rather than a separate population.
func _report_entity_edge(
		entity: NetwEntity,
		subject: RID,
		present: bool,
) -> void:
	var api := _api()
	if api == null or entity == null or not is_instance_valid(entity.owner):
		return
	var scene := api.scene_of(subject)
	if not scene.is_valid() or scene == subject:
		return
	_dispatch_observers(
		scene,
		NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY,
		present,
		subject,
	)
	if entity.peer_id != 0:
		_dispatch_observers(
			scene,
			NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER,
			present,
			subject,
		)


# Releases a player's admission once it is clear the player left for good. The
# check defers because tree_exiting cannot tell a free from a reparent: a node
# on its way to another scene is still valid a frame later, a freed one is not.
func _release_departed_player(
		scene: RID,
		entity: NetwEntity,
		peer: int,
) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	if is_instance_valid(entity) and is_instance_valid(entity.owner) \
			and api.scene_of(api.rid_of(entity.owner)) == scene:
		return
	release_peer(scene, peer)


# Calls every live observer for one edge, dropping those whose object is gone.
func _dispatch_observers(
		scene: RID,
		event: NetwMultiplayer.SceneEvent,
		present: bool,
		subject: Variant,
) -> void:
	core.dispatch(scene, event, present, subject)


func _free_retired_scene(scene_node: Node, drain_frames: int) -> void:
	var tree := Engine.get_main_loop() as SceneTree
	for i in drain_frames:
		if tree:
			await tree.process_frame
	if is_instance_valid(scene_node):
		scene_node.queue_free()


func _remove_scene(scene_node: Node) -> void:
	_live_scenes.erase(scene_node)
	for scene_name: StringName in scenes.keys():
		if scenes.get(scene_name) == scene_node:
			scenes.erase(scene_name)
	# A surviving sibling instance of the same stem keeps the stem answerable,
	# so retiring one arena does not make the other unreachable by name.
	for survivor: Node in _live_scenes:
		var content := _level_of(survivor)
		if content == null:
			continue
		var stem := StringName(content.name)
		if not scenes.has(stem):
			scenes[stem] = survivor
	refresh_current_scene.call_deferred()


# Runs one guarded replicated entity move.
func _move_entity(
		entity: NetwEntity,
		destination: Variant,
		opts: MoveOpts,
		promise: NetwPromise,
) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		promise.reject(ERR_UNAVAILABLE)
		return
	var target := _resolve_destination(destination)
	if target == null:
		promise.reject(ERR_UNAVAILABLE)
		return
	var source := scene_of(entity.owner)
	if source == target:
		promise.resolve(OK)
		return
	if opts == null:
		opts = MoveOpts.new()
	var guard := AreaReparentGuard.new(entity.owner)
	await guard.flush()
	var reparent_opts := NetwEntity.ReparentOpts.new()
	reparent_opts.preserve_history = opts.preserve_history
	reparent_opts.reason = opts.reason
	reparent_opts.target_global_position = opts.target_global_position
	entity.reparent_to(_level_of(target), reparent_opts)
	await guard.flush()
	guard.release()
	var participant := entity.participant
	if participant:
		participant.current_scene = _handle_for(target)
	var persistence := entity.persistence
	if persistence:
		persistence.flush()
	entity_moved.emit(entity, source, target)
	promise.resolve(OK)


# Replaces what the session presents. Every participant ends up in the target
# wherever it started, and every other live scene retires, so the answer does not
# depend on which scene happened to be first. A second transition entered while
# one is still moving peers resolves UNAVAILABLE so concurrent approvals never
# interleave moves against a half-transitioned roster. It holds the session-wide
# slot (_SESSION_TRANSITION), so it serializes against itself while two
# participants moving into two isolated worlds still proceed concurrently.
func _change_single_scene(
		destination: Variant,
		promise: NetwPromise,
) -> void:
	if _change_in_flight.has(_SESSION_TRANSITION):
		promise.reject(ERR_UNAVAILABLE)
		return
	var target := _existing_destination(destination)
	if target == null:
		_allow_single_spawn = true
		target = activate(destination)
		_allow_single_spawn = false
	if target == null:
		promise.reject(ERR_UNAVAILABLE)
		return
	var sources: Array[Node] = []
	for active: Node in _live_scenes:
		if is_instance_valid(active) and active != target:
			sources.append(active)
	_change_in_flight[_SESSION_TRANSITION] = true
	var moved_peers: Dictionary[int, bool] = { }
	for source: Node in sources:
		for entity: NetwEntity in _players_in(source):
			var move_promise := move(entity, target)
			if not move_promise.is_settled:
				await move_promise.settled
			if move_promise.code != OK:
				_change_in_flight.erase(_SESSION_TRANSITION)
				promise.reject(ERR_UNAVAILABLE)
				return
			moved_peers[entity.peer_id] = true
	var api := _api()
	var arrived := _handle_for(target)
	for participant: NetwParticipant in api.participants:
		if moved_peers.has(participant.peer_id):
			continue
		participant.current_scene = arrived
		var admitted := arrived.admit(participant)
		if admitted != OK:
			# A participant left outside the destination would present a scene it
			# is not in, so a partial transition fails rather than half-lands.
			_change_in_flight.erase(_SESSION_TRANSITION)
			promise.reject(admitted)
			return
	for source: Node in sources:
		var content := _level_of(source)
		if content != null:
			destroy(StringName(content.name))
	refresh_current_scene.call_deferred()
	_change_in_flight.erase(_SESSION_TRANSITION)
	promise.resolve(OK)


# Resolves and activates a destination reference.
func _resolve_destination(destination: Variant) -> Node:
	var existing := _existing_destination(destination)
	return existing if existing else activate(destination)


# Resolves an already active destination reference.
func _existing_destination(destination: Variant) -> Node:
	if destination is Node and _live_scenes.has(destination):
		return destination if is_instance_valid(destination) else null
	if destination is String or destination is StringName:
		return _container(StringName(destination))
	if destination is PackedScene:
		var path := (destination as PackedScene).resource_path
		if not path.is_empty():
			return _container(StringName(path.get_file().get_basename()))
	return null


# Server receive for a player scene request off the carrier.
func _handle_scene_request_frame(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	if _request_flooded(sender):
		return
	var data: Variant = bytes_to_var(payload)
	if not data is Array or (data as Array).size() != 4:
		return
	var request_id: int = data[0]
	var is_path: bool = data[1]
	var scene_ref: Variant = data[2]
	var args: Array = data[3] if data[3] is Array else []
	_receive_change_request(sender, request_id, is_path, scene_ref, args)


# Whether sender's scene requests exceed the flood window. The host at peer 1
# carries authority and is never limited.
func _request_flooded(sender: int) -> bool:
	# The wall clock is read here and handed down, so the window itself is a
	# function of its arguments.
	var flooded := _request_window.exceeded(sender, Time.get_ticks_msec())
	if flooded:
		var api := _api()
		if api:
			api._warn_gate_verdict(
				ERR_BUSY,
				0,
				"SceneCore: peer %d exceeded scene request rate",
				[sender],
			)
	return flooded


# Client receive for a server-authored request outcome off the carrier.
func _handle_scene_result_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		var api := _api()
		if api:
			api._warn_gate_verdict(
				ERR_UNAUTHORIZED,
				0,
				"SceneCore: rejected scene result from peer %d",
				[sender],
			)
		return
	var data: Variant = bytes_to_var(payload)
	if not data is Array or (data as Array).size() != 2:
		return
	_receive_change_result(data[0], data[1])


# Clears the local participant's scene membership from a server release notice.
func _handle_scene_released_frame(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var api := _api()
	if api == null or api.local_participant == null:
		return
	var released := StringName(bytes_to_var(payload))
	var current := api.local_participant.current_scene
	if current == null:
		return
	if api._scene_layer_id(current.entity) == released:
		api.local_participant.current_scene = null


# Applies server policy and answers one player request.
func _receive_change_request(
		peer_id: int,
		request_id: int,
		is_path: bool,
		scene_ref: Variant,
		args: Array,
) -> void:
	var api := _api()
	var participant := api.peer_get_participant(peer_id) if api else null
	if participant == null:
		_send_change_result(peer_id, request_id, ERR_UNAUTHORIZED)
		return
	if is_path:
		await _receive_path_request(
			peer_id,
			request_id,
			participant,
			String(scene_ref),
			args,
		)
	else:
		await _receive_named_request(
			peer_id,
			request_id,
			participant,
			StringName(scene_ref),
			args,
		)


# A declared-name request builds the wire context and authorizes it.
func _receive_named_request(
		peer_id: int,
		request_id: int,
		participant: NetwParticipant,
		scene_name: StringName,
		args: Array,
) -> void:
	var path := _scene_path_for(scene_name)
	var normalized := ResourceUID.ensure_path(path) if not path.is_empty() else ""
	var target_script := _scene_root_script_at(path)
	var config := NetwScriptModel.get_scene_config(target_script)
	if not _admits_request(
		participant,
		scene_name,
		normalized,
		args,
		target_script,
		config,
	):
		_send_change_result(peer_id, request_id, ERR_UNAUTHORIZED)
		return
	var operation := _apply_authorized_change(participant, scene_name, config)
	if not operation.is_settled:
		await operation.settled
	_send_change_result(peer_id, request_id, operation.code)


# A path request is bounded to a real scene file, then authorized against the
# target scene's mark. The mark is the consent line, so a marked non-gated
# scene admits by default and an installed request handler may still refuse it.
func _receive_path_request(
		peer_id: int,
		request_id: int,
		participant: NetwParticipant,
		scene_path: String,
		args: Array,
) -> void:
	var resolved := _verify_requested_path(scene_path)
	if resolved.is_empty():
		_send_change_result(
			peer_id,
			request_id,
			ERR_UNAVAILABLE,
		)
		return
	var packed := load(resolved) as PackedScene
	if packed == null:
		_send_change_result(
			peer_id,
			request_id,
			ERR_UNAVAILABLE,
		)
		return
	var target_script := _packed_root_script(packed)
	var config := NetwScriptModel.get_scene_config(target_script)
	if not _admits_request(
		participant,
		&"",
		resolved,
		args,
		target_script,
		config,
	):
		_send_change_result(peer_id, request_id, ERR_UNAUTHORIZED)
		return
	var operation := _apply_authorized_change(participant, packed, config)
	if not operation.is_settled:
		await operation.settled
	_send_change_result(peer_id, request_id, operation.code)


# Decides one player request. An installed handler answers outright, because a
# request has exactly one verdict. With none installed the mark is the consent
# line: a declared name and a marked scene admit, a raw unmarked path does not,
# and a gated or session-wide mark denies either.
func _admits_request(
		participant: NetwParticipant,
		scene_name: StringName,
		scene_path: String,
		args: Array,
		target_script: Script,
		config: NetwScriptModel.SceneMarkConfig,
) -> bool:
	var named := not scene_name.is_empty()
	if _request_handler.is_valid():
		var destination: Variant = scene_name if named else scene_path
		var verdict: Variant = _request_handler.call(
			participant,
			destination,
			args,
		)
		return (verdict is int or verdict is float) and int(verdict) == OK
	var gated := config != null and (config.is_gated or config.is_session_wide)
	var marked := target_script != null \
			and Netw.is_multiplayer_scene(target_script)
	return not scene_path.is_empty() and (named or marked) and not gated


# Applies an admitted request. A session-wide scene replaces the whole session,
# otherwise the configured reach drives whether one participant or everyone
# moves.
func _apply_authorized_change(
		participant: NetwParticipant,
		destination: Variant,
		config: NetwScriptModel.SceneMarkConfig,
) -> NetwPromise:
	if config and config.is_session_wide:
		return change_to(destination)
	return _apply_player_change(participant, destination)


# The root script of the scene at [param path], read from the packed state
# without instantiating it, so the mark can authorize before any spawn.
func _scene_root_script_at(path: String) -> Script:
	if path.is_empty():
		return null
	return _packed_root_script(load(ResourceUID.ensure_path(path)) as PackedScene)


# The root node's script of [param packed], or null.
func _packed_root_script(packed: PackedScene) -> Script:
	if packed == null:
		return null
	var state := packed.get_state()
	if state == null or state.get_node_count() == 0:
		return null
	for i in state.get_node_property_count(0):
		if state.get_node_property_name(0, i) == &"script":
			return state.get_node_property_value(0, i) as Script
	return null


# Normalizes a UID or resource path and bounds it to a real scene file.
func _verify_requested_path(scene_path: String) -> String:
	var resolved := ResourceUID.ensure_path(scene_path)
	if resolved.is_empty() or resolved.length() > 512:
		return ""
	if not resolved.begins_with("res://"):
		return ""
	var ext := resolved.get_extension()
	return resolved if ext == "tscn" or ext == "scn" else ""


# Applies an allowed request at the configured reach.
func _apply_player_change(
		participant: NetwParticipant,
		destination: Variant,
) -> NetwPromise:
	if request_reach == NetwMultiplayer.SceneReach.SCENE_REACH_SESSION \
			or participant == null:
		# A change with no participant to move names no smaller reach than the
		# session, which is what authority's own front door asks for.
		return change_to(destination)
	var target := _resolve_destination(destination)
	if target == null:
		var unavailable := NetwPromise.new()
		unavailable.reject(ERR_UNAVAILABLE)
		return unavailable
	for active: Node in scenes.values():
		for entity: NetwEntity in _players_in(active):
			if entity.peer_id == participant.peer_id:
				return move(entity, target)
	participant.move_to(_handle_for(target))
	var completed := NetwPromise.new()
	completed.resolve(OK)
	return completed


# Sends a terminal request result to the requesting peer off the carrier.
func _send_change_result(
		peer_id: int,
		request_id: int,
		result: Error,
) -> void:
	var api := _api()
	if api == null:
		return
	api._replication.send_to(
		peer_id,
		0,
		NetwFrameEnvelope.Channel.SESSION_SCENE_RESULT,
		var_to_bytes([request_id, int(result)]),
		true,
	)


# Resolves the one current local player request.
func _receive_change_result(request_id: int, result: int) -> void:
	if not core.is_current(request_id) or _pending_request == null:
		return
	_settle_promise(_pending_request, result as Error)
	_pending_request = null
	core.close_request()
	_settle_native_change(result)


# The canonical handle for one scene container, which is how a participant's
# membership is recorded now that the scalar is a handle rather than a node.
func _handle_for(scene_node: Node) -> NetwSceneHandle:
	var record := NetwEntity.of(scene_node)
	return record.scene if record else null


# The content root of [param scene_node], which is its only child. A scene with
# no content is a pure admission boundary and answers null.
func _level_of(scene_node: Node) -> Node:
	if not is_instance_valid(scene_node) or scene_node.get_child_count() == 0:
		return null
	return scene_node.get_child(0)


# Parents [param level] under [param scene_node] and names the container after
# it, which is what makes the container's only child its content root.
func _install_level(scene_node: Node, level: Node) -> void:
	scene_node.name = level.name + scene_node.name
	scene_node.add_child(level)
	level.owner = scene_node


# The player entities inside [param scene_node].
func _players_in(scene_node: Node) -> Array[NetwEntity]:
	var record := NetwEntity.of(scene_node)
	return record.scene.players if record else [] as Array[NetwEntity]


# Returns the owning API while it remains live.
func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# Returns the first live declaration config.
func _config() -> NetwSceneConfig:
	return _declared_config


# Resolves the scene this peer presents: the one its participant belongs to,
# falling back to the only live scene when membership has not landed yet.
func _resolve_current_scene() -> Node:
	var api := _api()
	if api == null or api.role == NetwMultiplayer.Role.DEDICATED_SERVER:
		return null
	if _local_participant and _local_participant.current_scene:
		return _local_participant.current_scene.level_container()
	for scene_node: Node in scenes.values():
		if is_instance_valid(scene_node):
			return scene_node
	return null


# Relays the participant edge and refreshes the current scene.
func _on_local_scene_changed(from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	local_scene_changed.emit(from, to)
	refresh_current_scene()


# Frees every active scene so a re-host rebuilds from empty, then clears local
# presentation. Freeing wrappers clears their layer memberships through normal
# entity lifecycle teardown, so the next session starts cleanly.
func _on_session_ended() -> void:
	for scene_node: Node in scenes.values().duplicate():
		if not is_instance_valid(scene_node):
			continue
		if scene_node.get_parent():
			scene_node.get_parent().remove_child(scene_node)
		scene_node.free()
	scenes.clear()
	if is_instance_valid(_host_scene_view):
		if _host_scene_view.get_parent():
			_host_scene_view.get_parent().remove_child(_host_scene_view)
		_host_scene_view.free()
	_host_scene_view = null
	if _local_participant and _local_participant.current_scene:
		_local_participant.current_scene = null
	_release_local_participant()
	_clear_request_state()
	_current_scene = null


# Resolves pending work and clears session scoped request policy.
func _clear_request_state() -> void:
	if _pending_request and not _pending_request.is_settled:
		_pending_request.reject(ERR_UNAVAILABLE)
	_pending_request = null
	core.close_request()
	_settle_native_change(ERR_UNAVAILABLE)


# Disconnects the current participant without scheduling another refresh.
func _release_local_participant() -> void:
	if _local_participant and _local_participant.scene_changed.is_connected(
		_on_local_scene_changed,
	):
		_local_participant.scene_changed.disconnect(_on_local_scene_changed)
	_local_participant = null


# Diagnoses native scene changes to a scene with no on-ramp during a live
# session. A scene configured through [method Netw.configure_multiplayer_scene]
# carries its own detach hook that converts the change into a server request, so
# it is exempt.
func _on_native_scene_changed(scene_root: Node) -> void:
	var api := _api()
	if api == null or api.state != NetwMultiplayer.SessionState.ONLINE:
		return
	if is_instance_valid(scene_root) \
			and NetwScriptModel.get_scene_config(scene_root.get_script()) != null:
		return
	push_error(
		"Native change_scene_to_* to an unmarked scene during an online "
		+ "session. The replicated session is intact, but this client left the "
		+ "presented game locally. Mark the scene with "
		+ "Netw.configure_multiplayer_scene() to make the change a server "
		+ "request, or change scenes with Netw.change_scene_to_file(), which "
		+ "applies on authority and asks from a client.",
	)
