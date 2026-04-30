@tool
class_name MultiplayerLobbyManager
extends MultiplayerSpawner

## Central authority that manages multiplayer lobbies for all connected players.
##
## Extends [MultiplayerSpawner] to replicate lobby scenes to clients.
## Add level scenes to the spawn list via the [member add_to_spawn_list] property.
## [codeblock]
## # Listen for lobbies becoming available:
## lobby_manager.lobby_spawned.connect(
##     func(lobby): print("Lobby ready: ", lobby.level.name)
## )
## # Activate an on-demand lobby before routing a player into it:
## await lobby_manager.activate_lobby(&"Level1")
## [/codeblock]

## Emitted when the manager has been initialized by the [MultiplayerTree].
signal configured()

## Emitted when a new [Lobby] has been instantiated and entered the tree.
signal lobby_spawned(lobby: Lobby)

## Emitted when a [Lobby] is removed from the tree.
signal lobby_despawned(lobby: Lobby)

const SERVER_LOBBY = preload("uid://dga0loylsa26i")
const CLIENT_LOBBY = preload("uid://cr2k17cu45app")
const VIEWPORTS_DEBUG = preload("uid://xu4dh3epglir")

## Controls when a lobby's level scene is loaded.
enum LoadMode {
	## Level is only loaded and spawned when the first player needs to enter it.
	ON_DEMAND = 0,
	## Level is loaded and spawned automatically when the server starts.
	ON_STARTUP = 1,
}

## Determines what happens to a lobby when its last player leaves.
enum EmptyAction {
	## The lobby continues processing normally.
	KEEP_ACTIVE = 0,
	## The level's process mode is set to [constant Node.PROCESS_MODE_DISABLED].
	FREEZE = 1,
	## The lobby is removed from the tree and freed.
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

## Per-lobby spawn data used by [method activate_lobby].
var lobby_spawn_data: Dictionary[StringName, Variant] = {}

## All currently active [Lobby] instances, keyed by their level's Node name.
var active_lobbies: Dictionary[StringName, Lobby]

## Lazily populated list of level scene file paths.
var lobbies: Array[String]:
	get:
		if lobbies.is_empty():
			if get_spawnable_scene_count() == 0:
				return []
			for scene_idx in get_spawnable_scene_count():
				lobbies.append(get_spawnable_scene(scene_idx))
			clear_spawnable_scenes()
		return lobbies

var _lobby_configs: Dictionary = {}
var _lobby_cache: Dictionary[String, PackedScene] = {}
var _lobby_paths: Dictionary[StringName, String] = {}


func _get_property_list() -> Array[Dictionary]:
	if not Engine.is_editor_hint():
		return []
	var props: Array[Dictionary] = []
	for i in get_spawnable_scene_count():
		var path := get_spawnable_scene(i)
		var basename := path.get_file().get_basename()
		props.append({
			"name": "lobby_config/%s/load_mode" % basename,
			"type": TYPE_INT,
			"hint": PROPERTY_HINT_ENUM,
			"hint_string": "ON_DEMAND,ON_STARTUP",
			"usage": PROPERTY_USAGE_DEFAULT,
		})
		props.append({
			"name": "lobby_config/%s/empty_action" % basename,
			"type": TYPE_INT,
			"hint": PROPERTY_HINT_ENUM,
			"hint_string": "KEEP_ACTIVE,FREEZE,DESTROY",
			"usage": PROPERTY_USAGE_DEFAULT,
		})
	return props


func _set(property: StringName, value: Variant) -> bool:
	var prop := str(property)
	if not prop.begins_with("lobby_config/"):
		return false
	var parts := prop.split("/")
	if parts.size() != 3:
		return false
	var level_name := StringName(parts[1])
	var key := parts[2]
	if not _lobby_configs.has(level_name):
		_lobby_configs[level_name] = {
			"load_mode": LoadMode.ON_STARTUP,
			"empty_action": EmptyAction.FREEZE,
		}
	_lobby_configs[level_name][key] = value
	return true


func _get(property: StringName) -> Variant:
	var prop := str(property)
	if not prop.begins_with("lobby_config/"):
		return null
	var parts := prop.split("/")
	if parts.size() != 3:
		return null
	var level_name := StringName(parts[1])
	var key := parts[2]
	if not _lobby_configs.has(level_name):
		return LoadMode.ON_STARTUP if key == "load_mode" else EmptyAction.FREEZE
	return _lobby_configs[level_name].get(key, null)


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
	lobby_spawned.connect(_on_lobby_spawned)
	lobby_despawned.connect(_on_lobby_despawned)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	spawn_function = _spawn_lobby_node
	spawn_path = "."
	add_to_group("lobby_managers")
	
	var mt: MultiplayerTree = get_parent()
	assert(
		is_instance_valid(mt), 
		"LobbyManager must be a direct child of MultiplayerTree"
	)
	mt.register_service(self, MultiplayerLobbyManager)
	
	if not mt.player_join_requested.is_connected(handle_join_request):
		mt.player_join_requested.connect(handle_join_request)
	
	if not mt.configured.is_connected(configured.emit):
		mt.configured.connect(configured.emit)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
		
	var mt: MultiplayerTree = get_parent()
	if is_instance_valid(mt):
		mt.unregister_service(self, MultiplayerLobbyManager)
		
		if mt.player_join_requested.is_connected(handle_join_request):
			mt.player_join_requested.disconnect(handle_join_request)
			
		if mt.configured.is_connected(configured.emit):
			mt.configured.disconnect(configured.emit)
			
	active_lobbies.clear()


## Returns the stored config for [param name].
func _get_config(name: StringName) -> Dictionary:
	if not _lobby_configs.has(name):
		return {"load_mode": LoadMode.ON_STARTUP, "empty_action": EmptyAction.FREEZE}
	return _lobby_configs[name]


## Loads the level scene for [param name] into a local cache.
func preload_lobby(name: StringName) -> void:
	var path := _lobby_paths.get(name, "")
	if path.is_empty():
		Netw.dbg.error("Cannot preload lobby '%s': not found." % name, func(m): push_error(m))
		return
	if _lobby_cache.has(path) or active_lobbies.has(name):
		return
	Netw.dbg.debug("Preloading lobby '%s' from '%s'." % [name, path])
	_lobby_cache[path] = load(path) as PackedScene
	Netw.dbg.info("Lobby '%s' preloaded." % name)


## Instantiates and adds [param name] to the scene tree.
func spawn_lobby(name: StringName) -> void:
	if active_lobbies.has(name):
		return
	var path := _lobby_paths.get(name, "")
	if path.is_empty():
		Netw.dbg.error("Cannot spawn lobby '%s': not found." % name, func(m): push_error(m))
		return
	Netw.dbg.info("Spawning lobby '%s'." % name)
	spawn(path)


## Ensures [param name] is spawned and forces its level's process mode to INHERIT.
func activate_lobby(name: StringName) -> void:
	Netw.dbg.trace("activate_lobby('%s') called." % name)
	if not active_lobbies.has(name):
		if level_spawn_function.is_valid():
			var data: Variant = lobby_spawn_data.get(name, name)
			spawn(data)
		else:
			spawn_lobby(name)
	
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby:
		Netw.dbg.error("Failed to activate lobby '%s'." % name, func(m): push_error(m))
		return
	
	lobby.level.process_mode = Node.PROCESS_MODE_INHERIT
	Netw.dbg.info("Lobby '%s' activated." % name)


## Sets the lobby level's process mode to DISABLED.
func freeze_lobby(name: StringName) -> void:
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby:
		Netw.dbg.warn("Cannot freeze lobby '%s': not active." % name, func(m): push_warning(m))
		return
	lobby.level.process_mode = Node.PROCESS_MODE_DISABLED
	Netw.dbg.info("Lobby '%s' frozen." % name)


## Removes and frees the lobby.
func destroy_lobby(name: StringName) -> void:
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby:
		Netw.dbg.warn("Cannot destroy lobby '%s': not active." % name, func(m): push_warning(m))
		return
	Netw.dbg.info("Destroying lobby '%s'." % name)
	if lobby.get_parent():
		lobby.get_parent().remove_child(lobby)
	lobby.queue_free()


## Returns an array of all active player nodes.
func get_all_players() -> Array[Node]:
	var players: Array[Node] = []
	for lobby: Lobby in active_lobbies.values():
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		for c in lobby.level.find_children("*", "SpawnerComponent", true, false):
			if is_instance_valid(c.owner):
				players.append(c.owner)
	return players


## Instantiates all lobbies configured as ON_STARTUP.
func spawn_lobbies() -> void:
	Netw.dbg.trace("spawn_lobbies called.")
	if not multiplayer.is_server():
		return
	_build_lobby_paths()
	if _lobby_paths.is_empty():
		Netw.dbg.warn("No lobby scenes are registered.", func(m): push_warning(m))
		return
	Netw.dbg.info("Checking %d lobby/lobbies for ON_STARTUP." % _lobby_paths.size())
	for name: StringName in _lobby_paths:
		if _get_config(name)["load_mode"] == LoadMode.ON_STARTUP:
			spawn_lobby(name)


## Spawn function used by [MultiplayerSpawner].
func _spawn_lobby_node(data: Variant) -> Node:
	var level: Node

	if level_spawn_function.is_valid():
		level = level_spawn_function.call(data)
		if not is_instance_valid(level):
			Netw.dbg.error("spawn function returned null.", func(m): push_error(m))
			return null
	elif data is String:
		var level_file_path: String = data
		Netw.dbg.info("Instantiating lobby node for: %s" % level_file_path)
		var level_scene: PackedScene
		if _lobby_cache.has(level_file_path):
			level_scene = _lobby_cache[level_file_path]
			_lobby_cache.erase(level_file_path)
		else:
			level_scene = load(level_file_path)
		level = level_scene.instantiate()
	else:
		Netw.dbg.error("invalid spawn data.", func(m): push_error(m))
		return null

	var lobby_scene: PackedScene = (SERVER_LOBBY
		if multiplayer.is_server() else CLIENT_LOBBY)
	var lobby: Lobby = lobby_scene.instantiate()

	lobby.level = level
	lobby.tree_entered.connect(lobby_spawned.emit.bind(lobby))
	lobby.tree_exited.connect(lobby_despawned.emit.bind(lobby))

	return lobby


## Called by [MultiplayerTree] to handle a player entry request.
func handle_join_request(client_data: MultiplayerClientData) -> void:
	var peer_id := client_data.peer_id
	for lobby: Lobby in active_lobbies.values():
		var sync := lobby.synchronizer
		if is_instance_valid(sync) and peer_id in sync.connected_clients:
			Netw.dbg.warn("Duplicate join from peer %d — ignored." % peer_id, func(m): push_warning(m))
			return
	
	Netw.dbg.info("Received join request from peer %d." % peer_id)
	var lobby_name := StringName(client_data.spawner_path.get_scene_name())
	await activate_lobby(lobby_name)

	var lobby := active_lobbies.get(lobby_name) as Lobby
	if not lobby:
		Netw.dbg.error("Join failed: Scene '%s' not registered." % lobby_name, func(m): push_error(m))
		return

	var spawner_client: SpawnerComponent = (
		lobby.level.get_node_or_null(client_data.spawner_path.node_path) as SpawnerComponent)
	assert(spawner_client, "Player needs a `SpawnerComponent`.")
	spawner_client.player_joined.emit(client_data)


func _apply_empty_action_if_needed(name: StringName) -> void:
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby or not lobby.synchronizer.connected_clients.is_empty():
		return
	var config := _get_config(name)
	match config["empty_action"]:
		EmptyAction.KEEP_ACTIVE:
			pass
		EmptyAction.FREEZE:
			freeze_lobby(name)
		EmptyAction.DESTROY:
			destroy_lobby(name)


func _on_lobby_spawned(node: Node) -> void:
	var lobby := node as Lobby
	Netw.dbg.info("Lobby spawned: %s" % lobby.level.name)
	active_lobbies[lobby.level.name] = lobby
	if multiplayer.is_server():
		lobby.synchronizer.despawned.connect(
			_on_player_left_lobby.bind(StringName(lobby.level.name)))
		_apply_empty_action_if_needed.call_deferred(StringName(lobby.level.name))


func _on_player_left_lobby(_player: Node, lobby_name: StringName) -> void:
	Netw.dbg.debug("Player left lobby '%s'. Evaluating empty action." % lobby_name)
	_apply_empty_action_if_needed(lobby_name)


func _on_lobby_despawned(node: Node) -> void:
	var lobby := node as Lobby
	Netw.dbg.info("Lobby despawned: %s" % lobby.level.name)
	active_lobbies.erase(lobby.level.name)


func _on_server_disconnected() -> void:
	Netw.dbg.info("Server disconnected. Cleaning up lobbies.")
	for lobby: Lobby in active_lobbies.values():
		if lobby.is_inside_tree():
			lobby.get_parent().remove_child(lobby)
		lobby.queue_free()


func _on_configured() -> void:
	Netw.dbg.trace("_on_configured called.")
	if not multiplayer.server_disconnected.is_connected(_on_server_disconnected):
		multiplayer.server_disconnected.connect(_on_server_disconnected)

	if multiplayer.is_server():
		var debug_viewports: Node = VIEWPORTS_DEBUG.instantiate()
		child_entered_tree.connect(debug_viewports.get("_on_node_entered"))
		child_exiting_tree.connect(debug_viewports.get("_on_node_exited"))
		add_child(debug_viewports)

		spawn_lobbies.call_deferred()


func _build_lobby_paths() -> void:
	if not _lobby_paths.is_empty():
		return
	for path: String in lobbies:
		var basename := StringName(path.get_file().get_basename())
		_lobby_paths[basename] = path
		Netw.dbg.debug("Registered lobby path: '%s' → '%s'." % [basename, path])


# Registers [param scene_path] as a single [constant LoadMode.ON_STARTUP]
# lobby, bypassing the inspector workflow.
# Called by [MultiplayerTree] when a world scene is dropped as a direct child.
func _configure_default(scene_path: String) -> void:
	var basename := StringName(scene_path.get_file().get_basename())
	_lobby_paths[basename] = scene_path
	_lobby_configs[basename] = {
		"load_mode": LoadMode.ON_STARTUP,
		"empty_action": EmptyAction.KEEP_ACTIVE,
	}
	Netw.dbg.debug(
		"Default lobby configured: '%s' -> '%s'.", [basename, scene_path]
	)


# Returns all scene paths set via [method _configure_default].
# Used to restore configuration after [method Node.duplicate].
func _get_configured_paths() -> Array[String]:
	var paths: Array[String] = []
	paths.assign(_lobby_paths.values())
	return paths
