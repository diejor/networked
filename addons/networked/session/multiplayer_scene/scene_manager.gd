@icon("res://addons/networked/assets/MultiplayerSceneManager.svg")
@tool
class_name MultiplayerSceneManager
extends Node
## Authoring node that declares a session's scenes for [SceneCore].
##
## The manager holds no runtime state. It snapshots its exported rows into a
## [NetwSceneConfig] and registers that with the session through
## [method NetwMultiplayer.object_configuration_add], so the session reads one
## declaration whether a manager authored the rows or a tree-less session built
## the config directly.
## [codeblock]
## # Inspector rows become a config the session reads.
## manager.scene_paths = ["res://level/lobby.tscn"]
##
## # The session spawns and moves scenes; the manager never does.
## api.scene(&"lobby").admit(participant)
## [/codeblock]

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

## Optional custom level constructor mirrored onto
## [member NetwSceneConfig.level_spawn_function], including after the config
## registers, so setting it from code at any time reaches the interface.
##
## Signature: [code]func(data: Variant) -> Node[/code]
var level_spawn_function: Callable:
	set(value):
		level_spawn_function = value
		if _netw_scene_config:
			_netw_scene_config.level_spawn_function = value

## Per-scene spawn data shared with [member NetwSceneConfig.spawn_data], so an
## in-place edit after the config registers reaches the interface.
var scene_spawn_data: Dictionary[StringName, Variant] = { }

## Declared level resource paths.
@export var scene_paths: Array[String] = []

## Isolation the declared scenes take. See [member NetwSceneConfig.isolation].
@export var scene_isolation: NetwMultiplayer.SceneIsolation = \
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE

## Scenes activated when the server session starts.
@export var initial_scene_paths: Array[String] = []

var _scene_paths: Dictionary[StringName, String] = { }
var _netw_scene_config: NetwSceneConfig


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	var mt := NetwService.register(self, MultiplayerSceneManager)
	assert(
		is_instance_valid(mt),
		"SceneManager must be a descendant of a MultiplayerTree",
	)
	_netw_scene_config = _build_netw_scene_config()
	mt.api.service_install(_netw_scene_config)


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


# Snapshots the declaration rows for the API-owned scene interface. Scalar rows
# copy once; the scene lists refresh through [method _sync_config] so paths
# registered after the config lands still reach the interface.
func _build_netw_scene_config() -> NetwSceneConfig:
	var config := NetwSceneConfig.new()
	config.isolation = scene_isolation
	config.level_spawn_function = level_spawn_function
	config.spawn_data = scene_spawn_data
	_populate_scene_lists(config)
	return config


# Rebuilds the config's scene name map and startup list from the current paths.
func _populate_scene_lists(config: NetwSceneConfig) -> void:
	config.scenes.clear()
	for path in get_configured_paths():
		var packed := load(path) as PackedScene
		if packed:
			config.scenes[StringName(path.get_file().get_basename())] = packed
	config.initial_scenes.clear()
	for stored_path: String in initial_scene_paths:
		var packed := load(ResourceUID.ensure_path(stored_path)) as PackedScene
		if packed:
			config.initial_scenes.append(packed)


# Refreshes the live config after a declaration changes.
func _sync_config() -> void:
	if _netw_scene_config:
		_populate_scene_lists(_netw_scene_config)


## Adds a resource path or UID [param path] to this declaration node.
func register_scene_path(path: String) -> void:
	if path.is_empty():
		return
	var resolved := ResourceUID.ensure_path(path)
	for stored_path: String in scene_paths:
		if ResourceUID.ensure_path(stored_path) == resolved:
			return
	scene_paths.append(path)
	_sync_config()


## Adds [param path] to the declaration rows and startup list.
func register_initial_scene_path(path: String) -> void:
	register_scene_path(path)
	if not initial_scene_paths.has(path):
		initial_scene_paths.append(path)
	_sync_config()


# Registers [param scene_path] as the default initial scene.
# Called by [MultiplayerTree] when a world scene is dropped as a direct child.
func _configure_default(scene_path: String) -> void:
	var basename := StringName(scene_path.get_file().get_basename())
	_scene_paths[basename] = scene_path
	initial_scene_paths = [scene_path]
	_sync_config()


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
