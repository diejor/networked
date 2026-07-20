## The session registry of active replicated scenes and the verbs that move them.
##
## [NetwMultiplayer] owns this interface from its constructor, so
## [member NetwMultiplayer.scenes] answers before any
## [MultiplayerSceneManager] node exists. A manager node only contributes
## [NetwSceneConfig] declaration rows. [member scenes] and [method scene_of]
## resolve active wrappers without one, and [member current_scene] reports what
## this peer presents independently from [member SceneTree.current_scene], which
## cannot reference a nested level. Every scene mutation runs on server
## authority. A client never calls [method activate], [method change_to], or
## [method move]. It asks through [method request_change] and awaits a
## [NetwScenePromise].
## [codeblock]
## # Any peer reads what it presents (null on a dedicated server).
## var here := api.scenes.current_scene
##
## # Server authority moves the world.
## api.scenes.activate("Arena")
## api.scenes.change_to("Arena")            # SINGLE: replace and carry everyone
##
## # A client asks; the server policy decides.
## var promise := api.scenes.request_change(&"Arena")
## await promise.completed
## [/codeblock]
##
## [constant NetwSceneConfig.Concurrency.SINGLE] keeps at most one scene active
## in the native viewport, so [method change_to] replaces it and moves every
## participant. [constant NetwSceneConfig.Concurrency.CONCURRENT] isolates each
## hosted world in an offscreen [SubViewport] and announces the displayed player
## through [signal NetwEntity.view_activated], so [method move] can relocate one
## entity while the others stay put.
class_name NetwSceneInterface
extends RefCounted

## Emitted when the local participant's primary scene changes.
signal local_scene_changed(from: MultiplayerScene, to: MultiplayerScene)

## Emitted when a constructed scene enters the tree on this peer.
signal scene_spawned(scene: MultiplayerScene)

## Emitted after the server's startup scenes have all spawned.
signal startup_scenes_spawned()

## Emitted when an active scene begins processing.
signal scene_activated(scene: MultiplayerScene)

## Emitted when an active scene leaves the session.
signal scene_despawned(scene: MultiplayerScene)

## Emitted when an active scene loses its last admitted peer.
signal scene_emptied(scene: MultiplayerScene)

## Emitted after [param entity] moves between replicated scenes.
signal entity_moved(
		entity: NetwEntity,
		from: MultiplayerScene,
		to: MultiplayerScene,
)

## Emitted when a captured native scene change settles with [param result].
##
## A native [method SceneTree.change_scene_to_file] to a scene marked through
## [method Netw.configure_multiplayer_scene] becomes a
## [method request_change_path] the server decides. This fires on every outcome,
## so a game that presents a loading screen from
## [method NetwScriptModel.SceneMarkConfig.on_pending] tears it down here even
## when the request resolves [constant NetwScenePromise.Result.DENIED] or
## [constant NetwScenePromise.Result.TIMED_OUT] and no scene ever arrives.
## [codeblock]
## func _init() -> void:
##     Netw.configure_multiplayer_scene(self).on_pending(_show_loading)
##
## # On the persistent HUD, not the scene being left:
## api.scenes.native_change_settled.connect(func(_result): hud.hide_loading())
## [/codeblock]
signal native_change_settled(result: NetwScenePromise.Result)

## Emitted on server authority before a player moves, carrying the pending
## [SceneChangeRequest] already at its default verdict. A listener overrides it
## with [method SceneChangeRequest.deny] or [method SceneChangeRequest.allow]. A
## declared name and a scene marked through
## [method Netw.configure_multiplayer_scene] admit by default; a raw path is
## admitted only when marked, and [method NetwScriptModel.SceneMarkConfig.gated]
## or [method NetwScriptModel.SceneMarkConfig.session_wide] flip to deny-default.
signal change_requested(request: SceneChangeRequest)

## Active scenes keyed by their level name.
var scenes: Dictionary[StringName, MultiplayerScene] = { }

## The scene currently presented on this peer, or [code]null[/code].
var current_scene: MultiplayerScene:
	get:
		return _current_scene if is_instance_valid(_current_scene) else null

## The registered concurrency mode.
var concurrency: NetwSceneConfig.Concurrency:
	get:
		var config := _config()
		return config.concurrency if config else \
		NetwSceneConfig.Concurrency.SINGLE

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
var _occupied_scenes: Dictionary[MultiplayerScene, bool] = { }
var _allow_single_spawn := false
var _constructor_registered := false
var _current_scene: MultiplayerScene
var _local_participant: NetwParticipant
# The tree-less host presentation view this interface parents under the session
# root, freed on session end. Stays null when an owning MultiplayerTree already
# created its own on its branch.
var _host_scene_view: HostSceneView
var _next_request_id := 1
var _pending_request_id := 0
var _pending_request: NetwScenePromise
var _pending_from_capture := false
var _change_in_flight := false
# Per-peer scene-request timestamps for the flood guard, pruned past
# _REQUEST_TRACKED_PEERS so a long-lived server never accumulates idle peers.
var _request_stamps: Dictionary = { }

## Seconds a player request waits for a server answer before resolving
## [constant NetwScenePromise.Result.TIMED_OUT].
const DEFAULT_REQUEST_DEADLINE := 10.0

## The registry id the interface registers its scene constructor under, so a
## session with no [MultiplayerSceneManager] still reconstructs scene wrappers.
const SCENE_CONSTRUCTOR_ID := &"__netw_scene__"

const _REQUEST_RATE_LIMIT := 8
const _REQUEST_TRACKED_PEERS := 64


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
	if not api.local_participant_joined.is_connected(bind_local_participant):
		api.local_participant_joined.connect(bind_local_participant)
	if not api.session.session_entered.is_connected(_on_session_entered):
		api.session.session_entered.connect(_on_session_entered)
	if not api.session.session_ended.is_connected(_on_session_ended):
		api.session.session_ended.connect(_on_session_ended)
	if not Netw.is_test_env():
		var scene_tree := Engine.get_main_loop() as SceneTree
		if scene_tree:
			if not scene_tree.scene_changed.is_connected(
				_on_native_scene_changed,
			):
				scene_tree.scene_changed.connect(_on_native_scene_changed)


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
		_local_participant.scene_changed.connect(_on_local_scene_changed)
	sync_local_participant.call_deferred()
	refresh_current_scene.call_deferred()


## Infers the local participant's scene from wrapper awareness.
func sync_local_participant() -> void:
	var api := _api()
	if _local_participant == null or api == null:
		return
	for scene_node: MultiplayerScene in scenes.values():
		if not is_instance_valid(scene_node):
			continue
		var layer := api.interest.get_layer(scene_node.scene_layer_id())
		var entity := NetwEntity.of(scene_node)
		if layer and entity and layer.has_entity(entity):
			_local_participant.current_scene = scene_node
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
		if api.session.session_entered.is_connected(_on_session_entered):
			api.session.session_entered.disconnect(_on_session_entered)
		if api.session.session_ended.is_connected(_on_session_ended):
			api.session.session_ended.disconnect(_on_session_ended)
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree and scene_tree.scene_changed.is_connected(
		_on_native_scene_changed,
	):
		scene_tree.scene_changed.disconnect(_on_native_scene_changed)
	_declared_config = null
	_occupied_scenes.clear()
	_scene_cache.clear()
	scenes.clear()


## Whether a session declared its scenes through a registered [NetwSceneConfig].
## A caller distinguishes a configured session from a bare one before the
## session goes online, so an auto-declaration for a dropped-in level defers to
## an explicit declaration.
func has_declaration() -> bool:
	return _config() != null


## Returns the active scene named [param scene_name], or [code]null[/code].
func scene(scene_name: StringName) -> MultiplayerScene:
	var active := scenes.get(scene_name) as MultiplayerScene
	return active if is_instance_valid(active) else null


## Returns the [MultiplayerScene] containing [param node], or [code]null[/code].
func scene_of(node: Node) -> MultiplayerScene:
	if not is_instance_valid(node):
		return null
	var current := node
	while current:
		if current is MultiplayerScene:
			return current as MultiplayerScene
		current = current.get_parent()
	return null


## Activates a declared scene name or a file-backed [PackedScene].
##
## A [PackedScene] does not need to be declared in advance. Returns the active
## wrapper, or [code]null[/code] when activation fails.
## [br][br][b]Server Only.[/b]
func activate(scene_ref: Variant) -> MultiplayerScene:
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
		var active := scene(scene_name)
		if active:
			active.level.process_mode = Node.PROCESS_MODE_INHERIT
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
func activate_scene(scene_name: StringName) -> MultiplayerScene:
	var api := _api()
	assert(api and api.is_server(), "Scene activation is server-only.")
	if scene(scene_name) == null:
		if level_spawn_function.is_valid():
			spawn(_spawn_data().get(scene_name, scene_name))
		else:
			spawn_scene(scene_name)
	var active := scene(scene_name)
	if active == null:
		Netw.dbg.error(
			"Failed to activate scene '%s'.",
			[scene_name],
			func(m): push_error(m),
		)
		return null
	active.level.process_mode = Node.PROCESS_MODE_INHERIT
	scene_activated.emit(active)
	return active


## Constructs and replicates the declared scene named [param scene_name].
## [br][br][b]Server Only.[/b]
func spawn_scene(scene_name: StringName) -> void:
	if scene(scene_name):
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


## Loads the declared scene named [param scene_name] into a local cache so a
## later activation instantiates it without a synchronous disk read.
func preload_scene(scene_name: StringName) -> void:
	var path := _scene_path_for(scene_name)
	if path.is_empty() or _scene_cache.has(path) or scene(scene_name):
		return
	_scene_cache[path] = load(path) as PackedScene


## Returns [code]true[/code] when [param scene_name] is cached by
## [method preload_scene].
func is_scene_preloaded(scene_name: StringName) -> bool:
	var path := _scene_path_for(scene_name)
	return not path.is_empty() and _scene_cache.has(path)


## Disables processing for the active scene named [param scene_name].
## [br][br][b]Server Only.[/b]
func freeze(scene_name: StringName) -> void:
	var api := _api()
	assert(api and api.is_server(), "Scene freezing is server-only.")
	var active := scene(scene_name)
	if active:
		active.level.process_mode = Node.PROCESS_MODE_DISABLED


## Removes the active scene named [param scene_name] from the session.
## [br][br][b]Server Only.[/b]
func destroy(scene_name: StringName) -> void:
	var api := _api()
	assert(api and api.is_server(), "Scene destruction is server-only.")
	var active := scene(scene_name)
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
	var active := scene(scene_name)
	if active == null:
		return
	_remove_scene(active)
	_free_retired_scene.call_deferred(active, maxi(0, drain_frames))


## Returns every active player [NetwEntity] across all active scenes.
func get_all_players() -> Array[NetwEntity]:
	var players: Array[NetwEntity] = []
	for active: MultiplayerScene in scenes.values():
		if not is_instance_valid(active):
			continue
		for entity: NetwEntity in active.get_players():
			if entity != null:
				players.append(entity)
	return players


## Returns the [MultiplayerScene] a freshly instantiated [param player] enters,
## honoring a stored teleport scene over [param fallback]. Awaits spawn
## hydration so persisted spawn state rides the SPAWN frame.
## [br][br][b]Server Only.[/b]
func resolve_hydrated_spawn_scene(
		player: Node,
		fallback: MultiplayerScene,
) -> MultiplayerScene:
	var api := _api()
	var entity := NetwEntity.of(player)
	if api and entity:
		var engine := api.persistence.engine_for(entity)
		if engine and engine.wants_spawn_hydration():
			await engine.hydrate()
	var tp: TPComponent = player.get_node_or_null("%TPComponent")
	if not tp or tp.current_scene_name.is_empty():
		return fallback
	var scene_name := StringName(tp.current_scene_name)
	if scene(scene_name) == null:
		await activate_scene(scene_name)
	var active := scene(scene_name)
	return active if active else fallback


## Moves [param entity] into [param destination].
##
## [param destination] accepts an active [MultiplayerScene], a declared scene
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
) -> NetwScenePromise:
	var api := _api()
	assert(api and api.is_server(), "Scene movement is server-only.")
	var promise := NetwScenePromise.new()
	_move_entity(entity, destination, opts, promise)
	return promise


## Replaces the active SINGLE scene and moves every participant into it.
##
## [param destination] accepts the same values as [method move].
## [br][br][b]Server Only.[/b]
func change_to(destination: Variant) -> NetwScenePromise:
	var api := _api()
	assert(api and api.is_server(), "Scene changes are server-only.")
	var promise := NetwScenePromise.new()
	_change_single_scene(destination, promise)
	return promise


## The muscle-memory front door mirroring [method SceneTree.change_scene_to_file].
##
## [param requester] carries the session and the local mover, so server
## authority runs the change directly while a client turns it into a
## [method request_change_path] the server decides. Both resolve to the same
## verbs, so the front door and the captured native change never diverge.
## [codeblock]
## var promise := api.scenes.change_scene_to_file(self, "res://arena.tscn")
## if await promise.completed != NetwScenePromise.Result.OK:
##     status.text = "Could not change scene."
## [/codeblock]
func change_scene_to_file(
		requester: Node,
		path: String,
) -> NetwScenePromise:
	return _front_door_change(requester, ResourceUID.ensure_path(path))


## The front door for a file-backed [PackedScene], mirroring
## [method SceneTree.change_scene_to_packed]. See [method change_scene_to_file].
func change_scene_to_packed(
		requester: Node,
		packed: PackedScene,
) -> NetwScenePromise:
	if packed == null or packed.resource_path.is_empty():
		push_error(
			"change_scene_to_packed needs a file-backed PackedScene so a "
			+ "client can request it by path.",
		)
		return _resolved_promise(NetwScenePromise.Result.UNAVAILABLE)
	return _front_door_change(requester, packed.resource_path)


## Re-enters the scene this peer currently presents, mirroring
## [method SceneTree.reload_current_scene]. See [method change_scene_to_file].
func reload_current_scene(requester: Node) -> NetwScenePromise:
	var here := current_scene
	if here == null or not is_instance_valid(here.level):
		push_error("reload_current_scene: this peer presents no scene.")
		return _resolved_promise(NetwScenePromise.Result.UNAVAILABLE)
	var path := here.level.scene_file_path
	if path.is_empty():
		push_error("reload_current_scene: the current scene has no file path.")
		return _resolved_promise(NetwScenePromise.Result.UNAVAILABLE)
	return _front_door_change(requester, ResourceUID.ensure_path(path))


# Routes a front-door change to authority's verb or a client's request. Server
# authority applies the change directly, honoring the concurrency mode; a client
# asks through the same request path the server policy decides.
func _front_door_change(requester: Node, path: String) -> NetwScenePromise:
	var api := _api()
	if api == null or path.is_empty():
		return _resolved_promise(NetwScenePromise.Result.UNAVAILABLE)
	if not api.is_server():
		return request_change_path(path)
	var packed := load(path) as PackedScene
	if packed == null:
		return _resolved_promise(NetwScenePromise.Result.UNAVAILABLE)
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
func _resolved_promise(result: NetwScenePromise.Result) -> NetwScenePromise:
	var promise := NetwScenePromise.new()
	promise.resolve(result)
	return promise


## Requests that server authority move the local player to [param scene_name].
##
## A newer request resolves the previous local promise as
## [constant NetwScenePromise.Result.SUPERSEDED]. A declared scene admits by
## default unless a [signal change_requested] listener vetoes it. A request
## unanswered within [constant DEFAULT_REQUEST_DEADLINE] resolves
## [constant NetwScenePromise.Result.TIMED_OUT].
## [br][br][b]Player request.[/b]
func request_change(
		scene_name: StringName,
		args: Array = [],
) -> NetwScenePromise:
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
) -> NetwScenePromise:
	return _start_request(true, scene_path, args)


# Supersedes any pending request, sends the frame, and arms the deadline.
func _start_request(
		is_path: bool,
		scene_ref: Variant,
		args: Array,
		deadline: float = DEFAULT_REQUEST_DEADLINE,
		from_capture: bool = false,
) -> NetwScenePromise:
	if _pending_request and not _pending_request.is_completed:
		_pending_request.resolve(NetwScenePromise.Result.SUPERSEDED)
		_settle_native_change(NetwScenePromise.Result.SUPERSEDED)
	_pending_from_capture = from_capture
	var promise := NetwScenePromise.new()
	var api := _api()
	if api == null:
		promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return promise
	_pending_request_id = _next_request_id
	_next_request_id += 1
	_pending_request = promise
	api.replication.send_to(
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
	timer.timeout.connect(_on_request_deadline.bind(request_id))


func _on_request_deadline(request_id: int) -> void:
	if request_id != _pending_request_id or _pending_request == null:
		return
	_pending_request.resolve(NetwScenePromise.Result.TIMED_OUT)
	_pending_request = null
	_pending_request_id = 0
	_settle_native_change(NetwScenePromise.Result.TIMED_OUT)


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
	if concurrency == NetwSceneConfig.Concurrency.SINGLE and _has_active_scene():
		change_to(packed)
	elif _is_concurrent_host_move_me():
		_apply_player_change(_api().local_participant, packed)
	else:
		activate(packed)


# Whether a bare native change on this peer means "move the local player": a
# listen host presenting concurrent worlds, with a local participant to move.
func _is_concurrent_host_move_me() -> bool:
	var api := _api()
	return api != null \
			and api.role == NetwSessionInterface.Role.LISTEN_SERVER \
			and concurrency == NetwSceneConfig.Concurrency.CONCURRENT \
			and api.local_participant != null


# Whether any scene is currently active.
func _has_active_scene() -> bool:
	for active: MultiplayerScene in scenes.values():
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
func _settle_native_change(result: NetwScenePromise.Result) -> void:
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
	api.replication.register_spawn_constructor(
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
# concurrency is fixed before the host view and startup spawns read it. Skipped
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
	manager.concurrency = NetwSceneConfig.Concurrency.SINGLE
	manager._configure_default(path)
	root.add_child(manager)


# A CONCURRENT listen-server host isolates its own world in an offscreen
# SubViewport that only a [HostSceneView] draws. It runs once from the settle
# step, keyed on the authored [member NetwSessionInterface.desired_role], and
# again when the session enters online, keyed on the resolved
# [member NetwSessionInterface.role], parenting the view under
# [member NetwMultiplayer.root] so it survives a native scene change alongside the
# session content. Idempotent, and skipped when a view already owns the display.
func _ensure_host_scene_view() -> void:
	var api := _api()
	if api == null:
		return
	if api.role != NetwSessionInterface.Role.LISTEN_SERVER \
			and api.session.desired_role != NetwSessionInterface.Role.LISTEN_SERVER:
		return
	if concurrency != NetwSceneConfig.Concurrency.CONCURRENT:
		return
	var root := api.root
	if root == null:
		return
	for child in root.get_children():
		if child is HostSceneView:
			return
	var view := HostSceneView.new()
	view.name = &"HostSceneView"
	_host_scene_view = view
	root.add_child(view)


func _spawn_initial_scenes() -> void:
	var config := _config()
	if config:
		for packed: PackedScene in config.initial_scenes:
			if packed and not packed.resource_path.is_empty():
				if _packed_scene_active(packed):
					continue
				spawn(packed.resource_path)
	startup_scenes_spawned.emit()


# Whether a scene with [param packed]'s root name is already active, so a startup
# spawn never duplicates a scene a join already brought online.
func _packed_scene_active(packed: PackedScene) -> bool:
	var state := packed.get_state()
	if state.get_node_count() == 0:
		return false
	return scene(StringName(state.get_node_name(0))) != null


## Constructs and replicates a scene wrapper from [param data], parenting it
## under the session anchor.
##
## [param data] is a resource path unless [member NetwSceneConfig.level_spawn_function]
## supplies a custom constructor, which receives [param data] verbatim on every
## peer. The host-less registry recipe reconstructs the wrapper with no manager
## node. Returns the active wrapper, or [code]null[/code] when the spawn fails.
## [br][br][b]Server Only.[/b]
func spawn(data: Variant) -> MultiplayerScene:
	var api := _api()
	assert(api and api.is_server(), "Scene spawning is server-only.")
	if not _can_spawn_scene(&""):
		return null
	var scene_node := api.replication.spawn_registered(
		SCENE_CONSTRUCTOR_ID,
		[data],
	) as MultiplayerScene
	if scene_node:
		var parent := _scene_parent()
		if parent:
			parent.add_child(scene_node)
	return scene_node


# The registered scene constructor, run on every peer to build an identical
# wrapper from the activation data alone.
func _spawn_scene_node(data: Variant) -> Node:
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
	var wrapper := _make_scene_wrapper(hosting)
	wrapper.level = level
	wrapper.tree_entered.connect(_on_scene_entered.bind(wrapper))
	wrapper.tree_exited.connect(_on_scene_exited.bind(wrapper))
	return wrapper


# Builds the wrapper the concurrency axis selects. A hosting CONCURRENT session
# isolates its world in a scripted [SubViewport]; every other case is a plain
# wrapper node.
func _make_scene_wrapper(hosting: bool) -> MultiplayerScene:
	var wrapper: MultiplayerScene
	if concurrency == NetwSceneConfig.Concurrency.CONCURRENT and hosting:
		var viewport := SubViewport.new()
		viewport.own_world_3d = true
		viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		viewport.set_script(MultiplayerScene)
		var scripted: Variant = viewport
		wrapper = scripted as MultiplayerScene
	else:
		wrapper = MultiplayerScene.new()
	wrapper.name = &"Scene"
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


# Enforces the one-active-scene invariant under SINGLE.
func _can_spawn_scene(scene_name: StringName) -> bool:
	if _allow_single_spawn:
		return true
	if concurrency != NetwSceneConfig.Concurrency.SINGLE \
			or not _has_active_scene():
		return true
	var detail := "another scene"
	if not scene_name.is_empty():
		detail = "scene '%s'" % scene_name
	Netw.dbg.error(
		"Cannot activate %s while a SINGLE scene is active. "
		+ "Use api.scenes.change_to(), or declare CONCURRENT.",
		[detail],
		func(m): push_error(m),
	)
	return false


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


func _on_scene_entered(scene_node: MultiplayerScene) -> void:
	if not is_instance_valid(scene_node) \
			or not is_instance_valid(scene_node.level):
		return
	var scene_name := StringName(scene_node.level.name)
	var existing := scene(scene_name)
	assert(
		existing == null or existing == scene_node,
		"Only one active MultiplayerScene may own a scene name.",
	)
	scenes[scene_name] = scene_node
	var api := _api()
	if api and api.is_server():
		scene_node.peer_admitted.connect(_on_peer_admitted.bind(scene_node))
		scene_node.despawned.connect(_on_player_left_scene.bind(scene_node))
		scene_node.peer_released.connect(_on_peer_left_scene.bind(scene_node))
	scene_spawned.emit(scene_node)
	sync_local_participant.call_deferred()
	refresh_current_scene.call_deferred()


func _on_scene_exited(scene_node: MultiplayerScene) -> void:
	_remove_scene(scene_node)
	scene_despawned.emit(scene_node)


func _on_peer_admitted(_peer_id: int, scene_node: MultiplayerScene) -> void:
	_occupied_scenes[scene_node] = true


func _on_player_left_scene(_player: Node, scene_node: MultiplayerScene) -> void:
	_emit_scene_emptied_if_needed.call_deferred(scene_node)


func _on_peer_left_scene(_peer_id: int, scene_node: MultiplayerScene) -> void:
	_emit_scene_emptied_if_needed.call_deferred(scene_node)


# Takes a [Variant] so a deferred call survives the scene freeing before it
# runs. A typed parameter would reject the freed object at the deferred call.
func _emit_scene_emptied_if_needed(scene_node: Variant) -> void:
	if not is_instance_valid(scene_node):
		return
	var scene := scene_node as MultiplayerScene
	if not _occupied_scenes.has(scene) \
			or not scene.connected_peers.is_empty():
		return
	_occupied_scenes.erase(scene)
	scene_emptied.emit(scene)


func _free_retired_scene(scene_node: MultiplayerScene, drain_frames: int) -> void:
	var tree := Engine.get_main_loop() as SceneTree
	for i in drain_frames:
		if tree:
			await tree.process_frame
	if is_instance_valid(scene_node):
		scene_node.queue_free()


func _remove_scene(scene_node: MultiplayerScene) -> void:
	_occupied_scenes.erase(scene_node)
	for scene_name: StringName in scenes.keys():
		if scenes.get(scene_name) == scene_node:
			scenes.erase(scene_name)
	refresh_current_scene.call_deferred()


# Runs one guarded replicated entity move.
func _move_entity(
		entity: NetwEntity,
		destination: Variant,
		opts: MoveOpts,
		promise: NetwScenePromise,
) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return
	var target := _resolve_destination(destination)
	if target == null:
		promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return
	var source := scene_of(entity.owner)
	if source == target:
		promise.resolve(NetwScenePromise.Result.OK)
		return
	if opts == null:
		opts = MoveOpts.new()
	var guard := AreaReparentGuard.new(entity.owner)
	await guard.flush()
	var reparent_opts := NetwEntity.ReparentOpts.new()
	reparent_opts.preserve_history = opts.preserve_history
	reparent_opts.reason = opts.reason
	reparent_opts.target_global_position = opts.target_global_position
	entity.reparent_to(target.level, reparent_opts)
	await guard.flush()
	guard.release()
	var participant := entity.participant
	if participant:
		participant.current_scene = target
	var persistence := entity.persistence
	if persistence:
		persistence.flush()
	entity_moved.emit(entity, source, target)
	promise.resolve(NetwScenePromise.Result.OK)


# Runs a whole-session SINGLE replacement. A second transition entered while
# one is still moving peers resolves UNAVAILABLE so concurrent approvals never
# interleave moves against a half-transitioned roster.
func _change_single_scene(
		destination: Variant,
		promise: NetwScenePromise,
) -> void:
	if concurrency != NetwSceneConfig.Concurrency.SINGLE:
		promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return
	if _change_in_flight:
		promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return
	var source: MultiplayerScene
	for active: MultiplayerScene in scenes.values():
		if is_instance_valid(active):
			source = active
			break
	var target := _existing_destination(destination)
	if target == null:
		_allow_single_spawn = true
		target = activate(destination)
		_allow_single_spawn = false
	if target == null:
		promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return
	if source == null or source == target:
		promise.resolve(NetwScenePromise.Result.OK)
		return
	_change_in_flight = true
	var moved_peers: Dictionary[int, bool] = { }
	for entity: NetwEntity in source.get_players():
		var move_promise := move(entity, target)
		if not move_promise.is_completed:
			await move_promise.completed
		if move_promise.result != NetwScenePromise.Result.OK:
			_change_in_flight = false
			promise.resolve(NetwScenePromise.Result.UNAVAILABLE)
			return
		moved_peers[entity.peer_id] = true
	var api := _api()
	for participant: NetwParticipant in api.participants:
		if moved_peers.has(participant.peer_id):
			continue
		participant.current_scene = target
		target.admit(participant)
	destroy(StringName(source.level.name))
	refresh_current_scene.call_deferred()
	_change_in_flight = false
	promise.resolve(NetwScenePromise.Result.OK)


# Resolves and activates a destination reference.
func _resolve_destination(destination: Variant) -> MultiplayerScene:
	var existing := _existing_destination(destination)
	return existing if existing else activate(destination)


# Resolves an already active destination reference.
func _existing_destination(destination: Variant) -> MultiplayerScene:
	if destination is MultiplayerScene:
		return destination if is_instance_valid(destination) else null
	if destination is String or destination is StringName:
		return scene(StringName(destination))
	if destination is PackedScene:
		var path := (destination as PackedScene).resource_path
		if not path.is_empty():
			return scene(StringName(path.get_file().get_basename()))
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
	if sender == 1:
		return false
	var now := Time.get_ticks_msec()
	var window_start := now - 1000
	if _request_stamps.size() > _REQUEST_TRACKED_PEERS:
		_prune_request_stamps(window_start)
	var stamps: Array = _request_stamps.get_or_add(sender, [] as Array[int])
	while not stamps.is_empty() and stamps[0] < window_start:
		stamps.pop_front()
	stamps.push_back(now)
	return stamps.size() > _REQUEST_RATE_LIMIT


# Drops peers whose newest request fell out of the current window.
func _prune_request_stamps(window_start: int) -> void:
	for peer_id: int in _request_stamps.keys():
		var stamps: Array = _request_stamps[peer_id]
		if stamps.is_empty() or stamps[stamps.size() - 1] < window_start:
			_request_stamps.erase(peer_id)


# Client receive for a server-authored request outcome off the carrier.
func _handle_scene_result_frame(payload: PackedByteArray, _sender: int) -> void:
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
	if not is_instance_valid(current):
		return
	if current.scene_layer_id() == released:
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
	var participant := api.participant(peer_id) if api else null
	if participant == null:
		_send_change_result(peer_id, request_id, NetwScenePromise.Result.DENIED)
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
	var rq := SceneChangeRequest.new(participant, scene_name, normalized, args)
	if not _admits_request(rq, target_script, config):
		_send_change_result(peer_id, request_id, NetwScenePromise.Result.DENIED)
		return
	var operation := _apply_authorized_change(participant, scene_name, config)
	if not operation.is_completed:
		await operation.completed
	_send_change_result(peer_id, request_id, operation.result)


# A path request is bounded to a real scene file, then authorized against the
# target scene's mark. The mark is the consent line, so a marked non-gated
# scene admits by default and a change_requested listener may still veto it.
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
			NetwScenePromise.Result.UNAVAILABLE,
		)
		return
	var packed := load(resolved) as PackedScene
	if packed == null:
		_send_change_result(
			peer_id,
			request_id,
			NetwScenePromise.Result.UNAVAILABLE,
		)
		return
	var target_script := _packed_root_script(packed)
	var config := NetwScriptModel.get_scene_config(target_script)
	var rq := SceneChangeRequest.new(participant, &"", resolved, args)
	if not _admits_request(rq, target_script, config):
		_send_change_result(peer_id, request_id, NetwScenePromise.Result.DENIED)
		return
	var operation := _apply_authorized_change(participant, packed, config)
	if not operation.is_completed:
		await operation.completed
	_send_change_result(peer_id, request_id, operation.result)


# Sets the admission default, then lets a change_requested listener override it.
# A named request is bounded to a declared scene, so it admits by default. A raw
# path request is arbitrary, so it needs the multiplayer-scene mark as its
# allowlist. A gated or session-wide mark flips either to deny-default. The
# listener then has the final say through allow() / deny().
func _admits_request(
		rq: SceneChangeRequest,
		target_script: Script,
		config: NetwScriptModel.SceneMarkConfig,
) -> bool:
	var gated := config != null and (config.is_gated or config.is_session_wide)
	var resolved := not rq.scene_path.is_empty()
	var named := not rq.scene_name.is_empty()
	var marked := target_script != null \
			and Netw.is_multiplayer_scene(target_script)
	rq.admitted = resolved and (named or marked) and not gated
	change_requested.emit(rq)
	return rq.admitted


# Applies an admitted request. A session-wide scene replaces the whole session,
# otherwise the concurrency mode drives whether one participant or everyone
# moves.
func _apply_authorized_change(
		participant: NetwParticipant,
		destination: Variant,
		config: NetwScriptModel.SceneMarkConfig,
) -> NetwScenePromise:
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


# Applies an allowed request under the configured concurrency mode.
func _apply_player_change(
		participant: NetwParticipant,
		destination: Variant,
) -> NetwScenePromise:
	if concurrency == NetwSceneConfig.Concurrency.SINGLE:
		return change_to(destination)
	var target := _resolve_destination(destination)
	if target == null:
		var unavailable := NetwScenePromise.new()
		unavailable.resolve(NetwScenePromise.Result.UNAVAILABLE)
		return unavailable
	for active: MultiplayerScene in scenes.values():
		for entity: NetwEntity in active.get_players():
			if entity.peer_id == participant.peer_id:
				return move(entity, target)
	participant.move_to(target)
	var completed := NetwScenePromise.new()
	completed.resolve(NetwScenePromise.Result.OK)
	return completed


# Sends a terminal request result to the requesting peer off the carrier.
func _send_change_result(
		peer_id: int,
		request_id: int,
		result: NetwScenePromise.Result,
) -> void:
	var api := _api()
	if api == null:
		return
	api.replication.send_to(
		peer_id,
		0,
		NetwFrameEnvelope.Channel.SESSION_SCENE_RESULT,
		var_to_bytes([request_id, int(result)]),
		true,
	)


# Resolves the one current local player request.
func _receive_change_result(request_id: int, result: int) -> void:
	if request_id != _pending_request_id or _pending_request == null:
		return
	_pending_request.resolve(result)
	_pending_request = null
	_pending_request_id = 0
	_settle_native_change(result)


# Returns the owning API while it remains live.
func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# Returns the first live declaration config.
func _config() -> NetwSceneConfig:
	return _declared_config


# Resolves the scene presented by this peer's role and concurrency mode.
func _resolve_current_scene() -> MultiplayerScene:
	var api := _api()
	if api == null or api.role == NetwSessionInterface.Role.DEDICATED_SERVER:
		return null
	if concurrency == NetwSceneConfig.Concurrency.SINGLE:
		for scene_node: MultiplayerScene in scenes.values():
			if is_instance_valid(scene_node):
				return scene_node
		return null
	if _local_participant and _local_participant.current_scene:
		return _local_participant.current_scene
	return null


# Relays the participant edge and refreshes the current scene.
func _on_local_scene_changed(from: MultiplayerScene, to: MultiplayerScene) -> void:
	local_scene_changed.emit(from, to)
	refresh_current_scene()


# Frees every active scene so a re-host rebuilds from empty, then clears local
# presentation. Freeing wrappers clears their layer memberships through normal
# entity lifecycle teardown, so the next session starts cleanly.
func _on_session_ended() -> void:
	for scene_node: MultiplayerScene in scenes.values().duplicate():
		if not is_instance_valid(scene_node):
			continue
		if scene_node.get_parent():
			scene_node.get_parent().remove_child(scene_node)
		scene_node.free()
	scenes.clear()
	_occupied_scenes.clear()
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
	if _pending_request and not _pending_request.is_completed:
		_pending_request.resolve(NetwScenePromise.Result.UNAVAILABLE)
	_pending_request = null
	_pending_request_id = 0
	_settle_native_change(NetwScenePromise.Result.UNAVAILABLE)


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
	if api == null or api.state != NetwSessionInterface.State.ONLINE:
		return
	if is_instance_valid(scene_root) \
			and NetwScriptModel.get_scene_config(scene_root.get_script()) != null:
		return
	push_error(
		"Native change_scene_to_* to an unmarked scene during an online "
		+ "session. The replicated session is intact, but this client left the "
		+ "presented game locally. Mark the scene with "
		+ "Netw.configure_multiplayer_scene() to make the change a server "
		+ "request, or use api.scenes.request_change() on a client or "
		+ "api.scenes.change_to() on the server.",
	)
