@icon("res://addons/networked/assets/MultiplayerSceneManager.svg")
@tool
class_name MultiplayerSceneManager
extends Node
## Central authority that manages multiplayer scenes for all connected players.
##
## Declared scenes are authoring conveniences. Scene replication carries the
## resource path and does not depend on matching registration order.
##
## [br][br]
## [b]Spawned scenes are not visible to peers until admitted.[/b]
## Even after the server activates a scene, the wrapper's
## [InterestGate] gates spawn-replication per peer. A peer only
## receives the scene after [method MultiplayerScene.connect_peer] is
## called for it, typically transitively via
## [method MultiplayerScene.register_player].
## See [MultiplayerScene] for the full invariant.
## [codeblock]
## # Listen for scenes becoming available:
## scene_manager.scene_spawned.connect(
##     func(scene): print("Scene ready: ", scene.level.name)
## )
## # Activate an on-demand scene before routing a player into it:
## await scene_manager.activate_scene(&"Level1")
## [/codeblock]

## Emitted when the manager has been initialized by the [MultiplayerTree].
signal configured()

## Emitted after [method spawn_scenes] has finished executing.
signal startup_scenes_spawned()

## Emitted when a new [Scene] has been instantiated and entered the tree.
signal scene_spawned(scene: MultiplayerScene)

## Emitted when an active scene's level is set to process normally.
signal scene_activated(scene: MultiplayerScene)

## Emitted when a [Scene] is removed from the tree.
signal scene_despawned(scene: MultiplayerScene)

## Emitted when a scene loses its last admitted peer.
signal scene_emptied(scene: MultiplayerScene)

const VIEWPORTS_DEBUG = preload("uid://xu4dh3epglir")
const MULTIPLAYER_SCENE_SCRIPT = preload(
	"res://addons/networked/session/multiplayer_scene/multiplayer_scene.gd"
)

## Helper property to add level declarations through the inspector.
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:Node",
)
var add_scene: SceneNodePath:
	set(value):
		if Engine.is_editor_hint() and value != null:
			var path: String = value.scene_path

			if not path.is_empty():
				if not scene_paths.has(path):
					scene_paths.append(path)
					notify_property_list_changed()

		add_scene = null

## Optional. Delegates level instantiation to this callable.
##
## Signature: [code]func(data: Variant) -> Node[/code]
var level_spawn_function: Callable

## Per-scene spawn data used by [method activate_scene].
var scene_spawn_data: Dictionary[StringName, Variant] = { }

## Declared level resource paths.
@export var scene_paths: Array[String] = []

## Scenes activated when the server session starts.
@export var initial_scene_paths: Array[String] = []

## Whether this session may keep one or several scenes active.
@export var concurrency: NetwSceneConfig.Concurrency = \
		NetwSceneConfig.Concurrency.SINGLE

## All currently active [Scene] instances, keyed by their level's Node name.
var active_scenes: Dictionary[StringName, MultiplayerScene]

var _scene_cache: Dictionary[String, PackedScene] = { }
var _scene_paths: Dictionary[StringName, String] = { }
var _debug_viewports: Node
var _netw_scene_config: NetwSceneConfig
var _occupied_scenes: Dictionary[MultiplayerScene, bool] = { }
var _allow_single_spawn := false


func _init() -> void:
	Netw.configure_spawn(_spawn_scene_node)

	if Engine.is_editor_hint():
		return

	configured.connect(_on_configured)
	scene_spawned.connect(_on_scene_spawned)
	scene_despawned.connect(_on_scene_despawned)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	add_to_group("scene_managers")

	var mt := NetwService.register(self, MultiplayerSceneManager)
	assert(
		is_instance_valid(mt),
		"SceneManager must be a descendant of a MultiplayerTree",
	)
	_netw_scene_config = _build_netw_scene_config()
	mt.api.object_configuration_add(self, _netw_scene_config)

	if not mt.session_entered.is_connected(configured.emit):
		mt.session_entered.connect(configured.emit)
	if not mt.session_ended.is_connected(_on_session_ended):
		mt.session_ended.connect(_on_session_ended)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	var mt := NetwService.unregister(self, MultiplayerSceneManager)
	assert(
		is_instance_valid(mt),
		"SceneManager must be a descendant of a MultiplayerTree",
	)
	if _netw_scene_config:
		mt.api.object_configuration_remove(self, _netw_scene_config)
		_netw_scene_config = null

	if mt.session_entered.is_connected(configured.emit):
		mt.session_entered.disconnect(configured.emit)
	if mt.session_ended.is_connected(_on_session_ended):
		mt.session_ended.disconnect(_on_session_ended)

	active_scenes.clear()


# Snapshots declaration rows for the API-owned scene interface.
func _build_netw_scene_config() -> NetwSceneConfig:
	var config := NetwSceneConfig.new()
	config.concurrency = concurrency
	for path in get_configured_paths():
		var packed := load(path) as PackedScene
		if packed:
			config.scenes[StringName(path.get_file().get_basename())] = packed
	for stored_path: String in initial_scene_paths:
		var packed := load(ResourceUID.ensure_path(stored_path)) as PackedScene
		if packed:
			config.initial_scenes.append(packed)
	config.spawn_data.assign(scene_spawn_data)
	return config


## Adds a resource path or UID [param path] to this declaration node.
func register_scene_path(path: String) -> void:
	if path.is_empty():
		return
	var resolved := ResourceUID.ensure_path(path)
	for stored_path: String in scene_paths:
		if ResourceUID.ensure_path(stored_path) == resolved:
			return
	scene_paths.append(path)


## Adds [param path] to the declaration rows and startup list.
func register_initial_scene_path(path: String) -> void:
	register_scene_path(path)
	if not initial_scene_paths.has(path):
		initial_scene_paths.append(path)


## Loads the level scene for [param name] into a local cache.
func preload_scene(name: StringName) -> void:
	var path := _scene_paths.get(name, "")
	if path.is_empty():
		Netw.dbg.error(
			"Cannot preload scene '%s': not found.",
			[name],
			func(m): push_error(m)
		)
		return
	if _scene_cache.has(path) or active_scenes.has(name):
		return
	Netw.dbg.debug("Preloading scene '%s' from '%s'.", [name, path])
	_scene_cache[path] = load(path) as PackedScene
	Netw.dbg.info("Scene '%s' preloaded.", [name])


## Returns [code]true[/code] when [param name] is cached by
## [method preload_scene].
func is_scene_preloaded(name: StringName) -> bool:
	var path := _scene_paths.get(name, "")
	return not path.is_empty() and _scene_cache.has(path)


## Instantiates and adds [param name] to the scene tree.
func spawn_scene(name: StringName) -> void:
	if active_scenes.has(name):
		return
	if not _can_spawn_scene(name):
		return
	var path := _scene_paths.get(name, "")
	if path.is_empty():
		Netw.dbg.error(
			"Cannot spawn scene '%s': not found.",
			[name],
			func(m): push_error(m)
		)
		return
	Netw.dbg.info("Spawning scene '%s'.", [name])
	spawn(path)


## Constructs and replicates a scene wrapper from [param data].
##
## [param data] is a resource path unless [member level_spawn_function]
## supplies a custom local constructor on every peer.
## [br][br][b]Server Only.[/b]
func spawn(data: Variant) -> MultiplayerScene:
	var api := NetwMultiplayer.of(self)
	assert(api and api.is_server(), "Scene spawning is server-only.")
	if not _can_spawn_scene(&""):
		return null
	var scene := api.replication.spawn(_spawn_scene_node, [data]) \
			as MultiplayerScene
	if scene:
		add_child(scene)
	return scene


# Enforces the one-active-scene invariant under SINGLE.
func _can_spawn_scene(name: StringName) -> bool:
	if _allow_single_spawn:
		return true
	if concurrency != NetwSceneConfig.Concurrency.SINGLE \
			or active_scenes.is_empty():
		return true
	var detail := "another scene"
	if not name.is_empty():
		detail = "scene '%s'" % name
	Netw.dbg.error(
		"Cannot activate %s while a SINGLE scene is active. "
		+ "Use api.scenes.change_to(), or declare CONCURRENT.",
		[detail],
		func(message): push_error(message),
	)
	return false


## Ensures [param name] is spawned and forces its level's process mode to
## [constant Node.PROCESS_MODE_INHERIT]. Returns the [MultiplayerScene], or
## [code]null[/code] if activation failed.
func activate_scene(name: StringName) -> MultiplayerScene:
	Netw.dbg.trace("activate_scene('%s') called.", [name])
	if not active_scenes.has(name):
		if level_spawn_function.is_valid():
			var data: Variant = scene_spawn_data.get(name, name)
			spawn(data)
		else:
			spawn_scene(name)

	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.error(
			"Failed to activate scene '%s'.",
			[name],
			func(m): push_error(m)
		)
		return null

	scene.level.process_mode = Node.PROCESS_MODE_INHERIT
	Netw.dbg.info("Scene '%s' activated.", [name])
	scene_activated.emit(scene)
	return scene


## Sets the scene level's process mode to DISABLED.
func freeze_scene(name: StringName) -> void:
	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.warn(
			"Cannot freeze scene '%s': not active.",
			[name],
			func(m): push_warning(m)
		)
		return
	scene.level.process_mode = Node.PROCESS_MODE_DISABLED
	Netw.dbg.info("Scene '%s' frozen.", [name])


## Removes and frees the scene.
func destroy_scene(name: StringName) -> void:
	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.warn(
			"Cannot destroy scene '%s': not active.",
			[name],
			func(m): push_warning(m)
		)
		return
	Netw.dbg.info("Destroying scene '%s'.", [name])
	if scene.get_parent():
		scene.get_parent().remove_child(scene)
	scene.queue_free()


## Removes [param name] from [member active_scenes] immediately, then frees the
## scene after [param drain_frames] process frames.
##
## Use this for teardown paths that must end game logic now while keeping stale
## replication paths resolvable for a short drain window.
func retire_scene(name: StringName, drain_frames: int = 8) -> void:
	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.warn(
			"Cannot retire scene '%s': not active.",
			[name],
			func(m): push_warning(m)
		)
		return
	Netw.dbg.info("Retiring scene '%s'.", [name])
	active_scenes.erase(name)
	_free_retired_scene.call_deferred(scene, maxi(0, drain_frames))


## Returns an array of all active player identities.
func get_all_players() -> Array[NetwEntity]:
	var players: Array[NetwEntity] = []
	for scene: MultiplayerScene in active_scenes.values():
		if not is_instance_valid(scene):
			continue
		for entity: NetwEntity in scene.get_players():
			if entity != null:
				players.append(entity)
	return players


## Instantiates every scene declared in [member initial_scene_paths].
func spawn_scenes() -> void:
	Netw.dbg.trace("spawn_scenes called.")
	if not multiplayer.is_server():
		return
	_build_scene_paths()
	if _scene_paths.is_empty():
		Netw.dbg.warn("No scene levels are registered.", func(m): push_warning(m))
		return
	for stored_path: String in initial_scene_paths:
		var path := ResourceUID.ensure_path(stored_path)
		spawn_scene(StringName(path.get_file().get_basename()))


# Spawn function carried by [NetwSpawnPipeline].
func _spawn_scene_node(data: Variant) -> Node:
	var level: Node

	if level_spawn_function.is_valid():
		level = level_spawn_function.call(data)
		if not is_instance_valid(level):
			Netw.dbg.error("spawn function returned null.", func(m): push_error(m))
			return null
	elif data is String:
		var level_file_path: String = data
		Netw.dbg.info("Instantiating scene node for: %s", [level_file_path])
		var level_scene: PackedScene
		if _scene_cache.has(level_file_path):
			level_scene = _scene_cache[level_file_path]
			_scene_cache.erase(level_file_path)
		else:
			level_scene = load(level_file_path)
		level = level_scene.instantiate()
	else:
		Netw.dbg.error("invalid spawn data.", func(m): push_error(m))
		return null

	var scene := _make_scene_wrapper(multiplayer.is_server())
	scene.level = level
	scene.tree_entered.connect(scene_spawned.emit.bind(scene))
	scene.tree_exited.connect(scene_despawned.emit.bind(scene))

	return scene


# Builds the native wrapper selected by the declared concurrency axis.
func _make_scene_wrapper(hosting: bool) -> MultiplayerScene:
	var scene: MultiplayerScene
	if concurrency == NetwSceneConfig.Concurrency.CONCURRENT \
			and hosting:
		var viewport := SubViewport.new()
		viewport.own_world_3d = true
		viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		viewport.set_script(MULTIPLAYER_SCENE_SCRIPT)
		var scripted_viewport: Variant = viewport
		scene = scripted_viewport as MultiplayerScene
	else:
		scene = MultiplayerScene.new()
	scene.name = &"Scene"
	var gate := InterestGate.new()
	gate.name = &"Gate"
	scene.add_child(gate)
	scene.gate = gate
	return scene


# Server-side. Returns the MultiplayerScene a freshly instantiated player should
# enter, honoring a TPComponent's stored scene over fallback_scene. Used by the
# session's join handler.
func _resolve_hydrated_spawn_scene(
		player: Node,
		fallback_scene: MultiplayerScene,
) -> MultiplayerScene:
	var mt := MultiplayerTree.resolve(self)
	var api := mt.api if mt else null
	var entity := NetwEntity.of(player)
	if api and entity:
		var engine := api.persistence.engine_for(entity)
		if engine and engine.wants_spawn_hydration():
			# The row applies before the SPAWN frame snapshots spawn state, so
			# persisted on_spawn fields ride the existing carrier to every peer.
			await engine.hydrate()
	var tp: TPComponent = player.get_node_or_null("%TPComponent")
	if not tp or tp.current_scene_name.is_empty():
		return fallback_scene
	var scene_name := StringName(tp.current_scene_name)
	if not active_scenes.has(scene_name):
		await activate_scene(scene_name)
	var scene := active_scenes.get(scene_name) as MultiplayerScene
	return scene if scene else fallback_scene


func _emit_scene_emptied_if_needed(scene_node: Variant) -> void:
	if not is_instance_valid(scene_node):
		return
	var scene := scene_node as MultiplayerScene
	if not _occupied_scenes.has(scene) or not scene.connected_peers.is_empty():
		return
	_occupied_scenes.erase(scene)
	scene_emptied.emit(scene)


func _on_scene_spawned(node: Node) -> void:
	var scene := node as MultiplayerScene
	Netw.dbg.info("Scene spawned: %s", [scene.level.name])
	active_scenes[scene.level.name] = scene
	if multiplayer.is_server():
		scene.peer_admitted.connect(_on_peer_admitted.bind(scene))
		scene.despawned.connect(
			_on_player_left_scene.bind(scene),
		)
		scene.peer_released.connect(
			_on_peer_left_scene.bind(scene),
		)


func _on_player_left_scene(_player: Node, scene: MultiplayerScene) -> void:
	_emit_scene_emptied_if_needed.call_deferred(scene)


func _on_peer_admitted(_peer_id: int, scene: MultiplayerScene) -> void:
	_occupied_scenes[scene] = true


func _on_peer_left_scene(_peer_id: int, scene: MultiplayerScene) -> void:
	_emit_scene_emptied_if_needed.call_deferred(scene)


func _on_scene_despawned(node: Node) -> void:
	var scene := node as MultiplayerScene
	Netw.dbg.info("Scene despawned: %s", [scene.level.name])
	_occupied_scenes.erase(scene)
	active_scenes.erase(scene.level.name)


func _free_retired_scene(scene: MultiplayerScene, drain_frames: int) -> void:
	for i in drain_frames:
		await get_tree().process_frame
	if is_instance_valid(scene):
		scene.queue_free()


func _on_configured() -> void:
	Netw.dbg.trace("_on_configured called.")

	var role := "server" if multiplayer.is_server() else "client"
	Netw.dbg.info(
		"SceneManager (%s): %d scene declaration(s): %s",
		[role, scene_paths.size(), scene_paths],
	)

	if multiplayer.is_server() \
			and concurrency == NetwSceneConfig.Concurrency.CONCURRENT:
		_debug_viewports = VIEWPORTS_DEBUG.instantiate()
		child_entered_tree.connect(_debug_viewports.get("_on_node_entered"))
		child_exiting_tree.connect(_debug_viewports.get("_on_node_exited"))
		add_child(_debug_viewports)

	if multiplayer.is_server():
		spawn_scenes.call_deferred()
		_emit_startup_scenes_spawned.call_deferred()


# Mirror of [method _on_configured]. Despawns every active scene so a re-host
# rebuilds from empty and frees the debug viewports node. Freeing each scene
# runs its [InterestGate]'s _exit_tree, which unregisters from
# [NetwInterestInterface] and clears the "layer already has a bound gate" error
# on the second session.
func _on_session_ended() -> void:
	for scene: MultiplayerScene in active_scenes.values().duplicate():
		if not is_instance_valid(scene):
			continue
		if scene.get_parent():
			scene.get_parent().remove_child(scene)
		scene.free()
	active_scenes.clear()

	if is_instance_valid(_debug_viewports):
		_debug_viewports.free()
	_debug_viewports = null


func _emit_startup_scenes_spawned() -> void:
	startup_scenes_spawned.emit()


func _build_scene_paths() -> void:
	if not _scene_paths.is_empty():
		return
	for path: String in get_configured_paths():
		var basename := StringName(path.get_file().get_basename())
		_scene_paths[basename] = path
		Netw.dbg.debug("Registered scene path: '%s' -> '%s'.", [basename, path])


# Registers [param scene_path] as the initial scene.
# Called by [MultiplayerTree] when a world scene is dropped as a direct child.
func _configure_default(scene_path: String) -> void:
	var basename := StringName(scene_path.get_file().get_basename())
	_scene_paths[basename] = scene_path
	initial_scene_paths = [scene_path]
	Netw.dbg.debug(
		"Default scene configured: '%s' -> '%s'.",
		[basename, scene_path],
	)


## Returns all registered scene paths. The list includes inspector entries,
## paths declared in [member scene_paths], and defaults registered by
## [MultiplayerTree] when a world scene is dropped as a direct child.
##
## Useful for restoring configuration after [method Node.duplicate] and
## for mirroring registration onto secondary scene managers (e.g. test
## harnesses spinning up client peers).
func get_configured_paths() -> Array[String]:
	var paths: Array[String] = []
	paths.assign(_scene_paths.values())
	for stored_path: String in scene_paths:
		var path := ResourceUID.ensure_path(stored_path)
		if not paths.has(path):
			paths.append(path)
	return paths
