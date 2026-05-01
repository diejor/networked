@tool
class_name MultiplayerSceneManager
extends MultiplayerSpawner

## Central authority that manages multiplayer scenes for all connected players.
##
## Extends [MultiplayerSpawner] to replicate scene levels to clients.
## Add level scenes to the spawn list via the [member add_to_spawn_list] property.
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

## Emitted when a new [Scene] has been instantiated and entered the tree.
signal scene_spawned(scene: MultiplayerScene)

## Emitted when a [Scene] is removed from the tree.
signal scene_despawned(scene: MultiplayerScene)

const SERVER_SCENE = preload("uid://dga0loylsa26i")
const CLIENT_SCENE = preload("uid://cr2k17cu45app")
const VIEWPORTS_DEBUG = preload("uid://xu4dh3epglir")

## Controls when a scene's level is loaded.
enum LoadMode {
	## Level is only loaded and spawned when the first player needs to enter it.
	ON_DEMAND = 0,
	## Level is loaded and spawned automatically when the server starts.
	ON_STARTUP = 1,
}

## Determines what happens to a scene when its last player leaves.
enum EmptyAction {
	## The scene continues processing normally.
	KEEP_ACTIVE = 0,
	## The level's process mode is set to [constant Node.PROCESS_MODE_DISABLED].
	FREEZE = 1,
	## The scene is removed from the tree and freed.
	DESTROY = 2,
}

## [b]Optional.[/b] The [TPLayerAPI] used for visual screen transitions.
@export var tp_layer: TPLayerAPI:
	set(layer):
		if not Engine.is_editor_hint():
			if tp_layer and configured.is_connected(tp_layer.configured.emit):
				configured.disconnect(tp_layer.configured.emit)

			tp_layer = layer

			if tp_layer and not configured.is_connected(
				tp_layer.configured.emit
			):
				configured.connect(tp_layer.configured.emit)
		else:
			tp_layer = layer

		update_configuration_warnings()

## Helper property to add level scenes to the spawn list via the inspector.
@export_custom(PROPERTY_HINT_ARRAY_TYPE, "24/17:SceneNodePath:MultiplayerSpawner")
var add_to_spawn_list: SceneNodePath:
	set(value):
		if Engine.is_editor_hint() and value != null:
			var path: String = value.scene_path

			if not path.is_empty():
				if not _has_spawnable_scene_path(path):
					add_spawnable_scene(path)
					notify_property_list_changed()

		add_to_spawn_list = null

## Optional. Delegates level instantiation to this callable.
##
## Signature: [code]func(data: Variant) -> Node[/code]
var level_spawn_function: Callable

## Per-scene spawn data used by [method activate_scene].
var scene_spawn_data: Dictionary[StringName, Variant] = {}

## All currently active [Scene] instances, keyed by their level's Node name.
var active_scenes: Dictionary[StringName, MultiplayerScene]

## Lazily populated list of level scene file paths.
var scene_paths: Array[String]:
	get:
		if scene_paths.is_empty():
			if get_spawnable_scene_count() == 0:
				return []
			for scene_idx in get_spawnable_scene_count():
				scene_paths.append(get_spawnable_scene(scene_idx))
			clear_spawnable_scenes()
		return scene_paths

var _scene_configs: Dictionary = {}
var _scene_cache: Dictionary[String, PackedScene] = {}
var _scene_paths: Dictionary[StringName, String] = {}


func _get_property_list() -> Array[Dictionary]:
	if not Engine.is_editor_hint():
		return []
	var props: Array[Dictionary] = []
	for i in get_spawnable_scene_count():
		var path := get_spawnable_scene(i)
		var basename := path.get_file().get_basename()
		props.append({
			"name": "scene_config/%s/load_mode" % basename,
			"type": TYPE_INT,
			"hint": PROPERTY_HINT_ENUM,
			"hint_string": "ON_DEMAND,ON_STARTUP",
			"usage": PROPERTY_USAGE_DEFAULT,
		})
		props.append({
			"name": "scene_config/%s/empty_action" % basename,
			"type": TYPE_INT,
			"hint": PROPERTY_HINT_ENUM,
			"hint_string": "KEEP_ACTIVE,FREEZE,DESTROY",
			"usage": PROPERTY_USAGE_DEFAULT,
		})
	return props


func _set(property: StringName, value: Variant) -> bool:
	var prop := str(property)
	if not prop.begins_with("scene_config/"):
		return false
	var parts := prop.split("/")
	if parts.size() != 3:
		return false
	var level_name := StringName(parts[1])
	var key := parts[2]
	if not _scene_configs.has(level_name):
		_scene_configs[level_name] = {
			"load_mode": LoadMode.ON_STARTUP,
			"empty_action": EmptyAction.FREEZE,
		}
	_scene_configs[level_name][key] = value
	return true


func _get(property: StringName) -> Variant:
	var prop := str(property)
	if not prop.begins_with("scene_config/"):
		return null
	var parts := prop.split("/")
	if parts.size() != 3:
		return null
	var level_name := StringName(parts[1])
	var key := parts[2]
	if not _scene_configs.has(level_name):
		return LoadMode.ON_STARTUP if key == "load_mode" else EmptyAction.FREEZE
	return _scene_configs[level_name].get(key, null)


func _has_spawnable_scene_path(target_path: String) -> bool:
	for i in get_spawnable_scene_count():
		if get_spawnable_scene(i) == target_path:
			return true
	return false


func _init() -> void:
	spawn_path = "."

	if Engine.is_editor_hint():
		return

	configured.connect(_on_configured)
	scene_spawned.connect(_on_scene_spawned)
	scene_despawned.connect(_on_scene_despawned)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	spawn_function = _spawn_scene_node
	spawn_path = "."
	add_to_group("scene_managers")

	var mt: MultiplayerTree = get_parent()
	assert(
		is_instance_valid(mt),
		"SceneManager must be a direct child of MultiplayerTree"
	)
	mt.register_service(self, MultiplayerSceneManager)

	if not mt.player_join_requested.is_connected(handle_join_request):
		mt.player_join_requested.connect(handle_join_request)

	if not mt.configured.is_connected(configured.emit):
		mt.configured.connect(configured.emit)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	var mt: MultiplayerTree = get_parent()
	if is_instance_valid(mt):
		mt.unregister_service(self, MultiplayerSceneManager)

		if mt.player_join_requested.is_connected(handle_join_request):
			mt.player_join_requested.disconnect(handle_join_request)

		if mt.configured.is_connected(configured.emit):
			mt.configured.disconnect(configured.emit)

	active_scenes.clear()


## Returns the stored config for [param name].
func _get_config(name: StringName) -> Dictionary:
	if not _scene_configs.has(name):
		return {"load_mode": LoadMode.ON_STARTUP, "empty_action": EmptyAction.FREEZE}
	return _scene_configs[name]


## Loads the level scene for [param name] into a local cache.
func preload_scene(name: StringName) -> void:
	var path := _scene_paths.get(name, "")
	if path.is_empty():
		Netw.dbg.error("Cannot preload scene '%s': not found." % name, func(m): push_error(m))
		return
	if _scene_cache.has(path) or active_scenes.has(name):
		return
	Netw.dbg.debug("Preloading scene '%s' from '%s'." % [name, path])
	_scene_cache[path] = load(path) as PackedScene
	Netw.dbg.info("Scene '%s' preloaded." % name)


## Instantiates and adds [param name] to the scene tree.
func spawn_scene(name: StringName) -> void:
	if active_scenes.has(name):
		return
	var path := _scene_paths.get(name, "")
	if path.is_empty():
		Netw.dbg.error("Cannot spawn scene '%s': not found." % name, func(m): push_error(m))
		return
	Netw.dbg.info("Spawning scene '%s'." % name)
	spawn(path)


## Ensures [param name] is spawned and forces its level's process mode to INHERIT.
func activate_scene(name: StringName) -> void:
	Netw.dbg.trace("activate_scene('%s') called." % name)
	if not active_scenes.has(name):
		if level_spawn_function.is_valid():
			var data: Variant = scene_spawn_data.get(name, name)
			spawn(data)
		else:
			spawn_scene(name)

	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.error("Failed to activate scene '%s'." % name, func(m): push_error(m))
		return

	scene.level.process_mode = Node.PROCESS_MODE_INHERIT
	Netw.dbg.info("Scene '%s' activated." % name)


## Sets the scene level's process mode to DISABLED.
func freeze_scene(name: StringName) -> void:
	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.warn("Cannot freeze scene '%s': not active." % name, func(m): push_warning(m))
		return
	scene.level.process_mode = Node.PROCESS_MODE_DISABLED
	Netw.dbg.info("Scene '%s' frozen." % name)


## Removes and frees the scene.
func destroy_scene(name: StringName) -> void:
	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene:
		Netw.dbg.warn("Cannot destroy scene '%s': not active." % name, func(m): push_warning(m))
		return
	Netw.dbg.info("Destroying scene '%s'." % name)
	if scene.get_parent():
		scene.get_parent().remove_child(scene)
	scene.queue_free()


## Returns an array of all active player nodes.
func get_all_players() -> Array[Node]:
	var players: Array[Node] = []
	for scene: MultiplayerScene in active_scenes.values():
		if not is_instance_valid(scene) or not is_instance_valid(scene.level):
			continue
		for c in scene.level.find_children("*", "SpawnerComponent", true, false):
			if is_instance_valid(c.owner):
				players.append(c.owner)
	return players


## Instantiates all scenes configured as ON_STARTUP.
func spawn_scenes() -> void:
	Netw.dbg.trace("spawn_scenes called.")
	if not multiplayer.is_server():
		return
	_build_scene_paths()
	if _scene_paths.is_empty():
		Netw.dbg.warn("No scene levels are registered.", func(m): push_warning(m))
		return
	Netw.dbg.info("Checking %d scene levels for ON_STARTUP." % _scene_paths.size())
	for name: StringName in _scene_paths:
		if _get_config(name)["load_mode"] == LoadMode.ON_STARTUP:
			spawn_scene(name)


## Spawn function used by [MultiplayerSpawner].
func _spawn_scene_node(data: Variant) -> Node:
	var level: Node

	if level_spawn_function.is_valid():
		level = level_spawn_function.call(data)
		if not is_instance_valid(level):
			Netw.dbg.error("spawn function returned null.", func(m): push_error(m))
			return null
	elif data is String:
		var level_file_path: String = data
		Netw.dbg.info("Instantiating scene node for: %s" % level_file_path)
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

	var scene_scene: PackedScene = (SERVER_SCENE
		if multiplayer.is_server() else CLIENT_SCENE)
	var scene: MultiplayerScene = scene_scene.instantiate()

	scene.level = level
	scene.tree_entered.connect(scene_spawned.emit.bind(scene))
	scene.tree_exited.connect(scene_despawned.emit.bind(scene))

	return scene


## Activates the target scene for [param client_data] and returns it.
##
## Checks for duplicate connections. Returns the active
## [MultiplayerScene], or [code]null[/code] on failure.
## [br][br]
## Use for custom spawn flows that don't use [SpawnerComponent].
func activate_scene_for(
	client_data: MultiplayerClientData
) -> MultiplayerScene:
	var peer_id := client_data.peer_id
	for scene: MultiplayerScene in active_scenes.values():
		var sync := scene.synchronizer
		if is_instance_valid(sync) and peer_id in sync.connected_clients:
			Netw.dbg.warn(
				"Duplicate join from peer %d — ignored." % peer_id,
				func(m): push_warning(m)
			)
			return null

	Netw.dbg.info(
		"Received join request from peer %d." % peer_id
	)
	var scene_name := StringName(
		client_data.spawner_path.get_scene_name()
	)
	await activate_scene(scene_name)

	var scene := active_scenes.get(scene_name) as MultiplayerScene
	if not scene:
		Netw.dbg.error(
			"Join failed: Scene '%s' not registered." % scene_name,
			func(m): push_error(m)
		)
		return null

	return scene


## Called by [MultiplayerTree] to handle a player entry request.
##
## Activates the target scene and emits
## [signal SpawnerComponent.player_joined].
func handle_join_request(client_data: MultiplayerClientData) -> void:
	var scene := await activate_scene_for(client_data)
	if not scene:
		return

	var spawner_client: SpawnerComponent = (
		scene.level.get_node_or_null(
			client_data.spawner_path.node_path
		) as SpawnerComponent
	)
	assert(spawner_client, "Player needs a `SpawnerComponent`.")
	spawner_client.player_joined.emit(client_data)


func _apply_empty_action_if_needed(name: StringName) -> void:
	var scene := active_scenes.get(name) as MultiplayerScene
	if not scene or not scene.synchronizer.connected_clients.is_empty():
		return
	var config := _get_config(name)
	match config["empty_action"]:
		EmptyAction.KEEP_ACTIVE:
			pass
		EmptyAction.FREEZE:
			freeze_scene(name)
		EmptyAction.DESTROY:
			destroy_scene(name)


func _on_scene_spawned(node: Node) -> void:
	var scene := node as MultiplayerScene
	Netw.dbg.info("Scene spawned: %s" % scene.level.name)
	active_scenes[scene.level.name] = scene
	if multiplayer.is_server():
		scene.synchronizer.despawned.connect(
			_on_player_left_scene.bind(StringName(scene.level.name)))
		_apply_empty_action_if_needed.call_deferred(StringName(scene.level.name))


func _on_player_left_scene(_player: Node, scene_name: StringName) -> void:
	Netw.dbg.debug("Player left scene '%s'. Evaluating empty action." % scene_name)
	_apply_empty_action_if_needed(scene_name)


func _on_scene_despawned(node: Node) -> void:
	var scene := node as MultiplayerScene
	Netw.dbg.info("Scene despawned: %s" % scene.level.name)
	active_scenes.erase(scene.level.name)


func _on_server_disconnected() -> void:
	Netw.dbg.info("Server disconnected. Cleaning up scenes.")
	for scene: MultiplayerScene in active_scenes.values():
		if scene.is_inside_tree():
			scene.get_parent().remove_child(scene)
		scene.queue_free()


func _on_configured() -> void:
	Netw.dbg.trace("_on_configured called.")
	if not multiplayer.server_disconnected.is_connected(_on_server_disconnected):
		multiplayer.server_disconnected.connect(_on_server_disconnected)

	if multiplayer.is_server():
		var debug_viewports: Node = VIEWPORTS_DEBUG.instantiate()
		child_entered_tree.connect(debug_viewports.get("_on_node_entered"))
		child_exiting_tree.connect(debug_viewports.get("_on_node_exited"))
		add_child(debug_viewports)

		spawn_scenes.call_deferred()


func _build_scene_paths() -> void:
	if not _scene_paths.is_empty():
		return
	for path: String in scene_paths:
		var basename := StringName(path.get_file().get_basename())
		_scene_paths[basename] = path
		Netw.dbg.debug("Registered scene path: '%s' → '%s'." % [basename, path])


# Registers [param scene_path] as a single [constant LoadMode.ON_STARTUP]
# scene, bypassing the inspector workflow.
# Called by [MultiplayerTree] when a world scene is dropped as a direct child.
func _configure_default(scene_path: String) -> void:
	var basename := StringName(scene_path.get_file().get_basename())
	_scene_paths[basename] = scene_path
	_scene_configs[basename] = {
		"load_mode": LoadMode.ON_STARTUP,
		"empty_action": EmptyAction.KEEP_ACTIVE,
	}
	Netw.dbg.debug(
		"Default scene configured: '%s' -> '%s'.", [basename, scene_path]
	)


# Returns all scene paths set via [method _configure_default].
# Used to restore configuration after [method Node.duplicate].
func _get_configured_paths() -> Array[String]:
	var paths: Array[String] = []
	paths.assign(_scene_paths.values())
	return paths
