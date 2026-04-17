@tool
class_name MultiplayerLobbyManager
extends MultiplayerSpawner

## Central authority that spawns and manages multiplayer lobbies for all connected players.
##
## Extends [MultiplayerSpawner] to replicate lobby scenes to clients automatically.
## Add level scenes to the spawn list via the [member add_to_spawn_list] helper property in the
## inspector. Each level gets two per-level dropdowns in the Inspector:
## [b]Load Mode[/b] ([enum LoadMode]) and [b]Empty Action[/b] ([enum EmptyAction]).
## [codeblock]
## # Listen for lobbies becoming available:
## lobby_manager.lobby_spawned.connect(func(lobby): print("Lobby ready: ", lobby.level.name))
## # Activate an on-demand lobby before routing a player into it:
## await lobby_manager.activate_lobby(&"Level1")
## [/codeblock]

## Emitted when the manager has been successfully initialized by the [MultiplayerTree].
signal configured()

## Emitted when a new [Lobby] has been instantiated and entered the tree.
signal lobby_spawned(lobby: Lobby)

## Emitted when a [Lobby] is removed from the tree.
signal lobby_despawned(lobby: Lobby)

const SERVER_LOBBY = preload("uid://dga0loylsa26i")
const CLIENT_LOBBY = preload("uid://cr2k17cu45app")
const VIEWPORTS_DEBUG = preload("uid://xu4dh3epglir")

## Controls when a lobby's level scene is loaded relative to server startup.
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
	## The level's process mode is set to [constant Node.PROCESS_MODE_DISABLED], saving CPU.
	FREEZE = 1,
	## The lobby is removed from the tree and freed, saving RAM.
	DESTROY = 2,
}

## [b]Optional.[/b] The [TPLayerAPI] used for visual screen transitions during teleportation.
##
## If unassigned, players are still moved between lobbies but no fade animation plays.
@export var tp_layer: TPLayerAPI:
	set(layer):
		if not Engine.is_editor_hint():
			if tp_layer and configured.is_connected(tp_layer.configured.emit):
				configured.disconnect(tp_layer.configured.emit)

			tp_layer = layer

			if tp_layer and not configured.is_connected(
				tp_layer.configured.emit):
				configured.connect(tp_layer.configured.emit)
		else:
			tp_layer = layer

		update_configuration_warnings()

## Helper property to add level scenes to the spawn list via the inspector.
##
## TODO: Verify that the target MultiplayerSpawner has `spawn_path` set to the root,
## assert or make editor warning to warn the user that the system will not work as expected.
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

## Optional. If set, delegates level instantiation to this callable instead of the
## default path-based loading. Called on both server and clients during spawn.
## [br][br]
## Signature: [code]func(data: Variant) -> Node[/code]
## [br][br]
## The returned node's [member Node.name] MUST be deterministic and match the lobby name
## used in [method activate_lobby] and the scene basename clients embed in their SpawnerPath —
## it becomes the key in [member active_lobbies].
## [br][br]
## When set, automatic ON_STARTUP spawning via [method spawn_lobbies] is not supported.
## Call [method spawn] manually in [signal configured] for lobbies that should exist at startup.
var level_spawn_function: Callable

## Per-lobby spawn data used by [method activate_lobby] when [member level_spawn_function] is set.
## [br][br]
## Keys are lobby names ([StringName]). Values are the [code]data[/code] argument passed to
## [member level_spawn_function] and replicated to clients via [MultiplayerSpawner].
## If a lobby name has no entry here, the name itself is passed as data.
var lobby_spawn_data: Dictionary[StringName, Variant] = {}

## All currently active [Lobby] instances, keyed by their level's [member Node.name].
var active_lobbies: Dictionary[StringName, Lobby]

## Lazily populated list of level scene file paths from the spawnable scene list.
var lobbies: Array[String]:
	get:
		if lobbies.is_empty():
			if get_spawnable_scene_count() == 0:
				return []
			for scene_idx in get_spawnable_scene_count():
				lobbies.append(get_spawnable_scene(scene_idx))
			clear_spawnable_scenes()
		return lobbies

## Per-level load mode and empty-action configuration.
##
## Keys are the level basenames (scene filename without extension).
## Values are Dictionaries with keys [code]"load_mode"[/code] and [code]"empty_action"[/code].
var _lobby_configs: Dictionary = {}

## Scenes preloaded by [method preload_lobby] but not yet instantiated.
var _lobby_cache: Dictionary[String, PackedScene] = {}

## Maps each level basename to its scene file path. Built lazily in [method spawn_lobbies].
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


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()

	if not tp_layer:
		warnings.append("Optional: No TPLayer is provided. Teleportation and \
scene transitions will not work visually. You can use TPLayer Scene to test \
functionality.")

	return warnings


func _init() -> void:
	if Engine.is_editor_hint():
		return

	configured.connect(_on_configured)
	lobby_spawned.connect(_on_lobby_spawned)
	lobby_despawned.connect(_on_lobby_despawned)


func _ready() -> void:
	if Engine.is_editor_hint():
		return

	spawn_function = _spawn_lobby_node
	spawn_path = "."
	add_to_group("lobby_managers")


func _exit_tree() -> void:
	if not Engine.is_editor_hint():
		active_lobbies.clear()


## Returns the stored config for [param name], falling back to safe defaults.
func _get_config(name: StringName) -> Dictionary:
	if not _lobby_configs.has(name):
		return {"load_mode": LoadMode.ON_STARTUP, "empty_action": EmptyAction.FREEZE}
	return _lobby_configs[name]


## Loads the level scene for [param name] into a local cache without instantiating it.
##
## Useful for proximity-based preloading. A cached scene is consumed on the next
## [method spawn_lobby] call, eliminating disk I/O at spawn time.
func preload_lobby(name: StringName) -> void:
	var path := _lobby_paths.get(name, "")
	if path.is_empty():
		NetLog.error(func(): push_error("MultiplayerLobbyManager: Cannot preload lobby '%s': path not found." % name))
		return
	if _lobby_cache.has(path) or active_lobbies.has(name):
		return
	NetLog.debug("Preloading lobby '%s' from '%s'." % [name, path])
	_lobby_cache[path] = load(path) as PackedScene
	NetLog.info("Lobby '%s' preloaded." % name)


## Instantiates and adds [param name] to the scene tree.
##
## Does nothing if the lobby is already active. Callers that need to guarantee the lobby
## is processing should use [method activate_lobby] instead.
func spawn_lobby(name: StringName) -> void:
	if active_lobbies.has(name):
		return
	var path := _lobby_paths.get(name, "")
	if path.is_empty():
		NetLog.error(func(): push_error("MultiplayerLobbyManager: Cannot spawn lobby '%s': path not found." % name))
		return
	NetLog.info("Spawning lobby '%s'." % name)
	spawn(path)


## Ensures [param name] is spawned and forces its level's process mode to [constant Node.PROCESS_MODE_INHERIT].
##
## Spawns the lobby on demand if not yet active, then wakes the level regardless of its
## configured [enum EmptyAction]. Safe to [operator await] from any context.
## [br][br]
## When [member level_spawn_function] is set, spawn data is looked up from
## [member lobby_spawn_data]; if no entry exists for [param name], the name itself is passed.
func activate_lobby(name: StringName) -> void:
	NetLog.trace("MultiplayerLobbyManager: activate_lobby('%s') called." % name)
	if not active_lobbies.has(name):
		if level_spawn_function.is_valid():
			var data: Variant = lobby_spawn_data.get(name, name)
			spawn(data)
		else:
			spawn_lobby(name)
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby:
		NetLog.error(func(): push_error("MultiplayerLobbyManager: Failed to activate lobby '%s'." % name))
		return
	lobby.level.process_mode = Node.PROCESS_MODE_INHERIT
	NetLog.info("Lobby '%s' activated." % name)


## Sets the lobby level's process mode to [constant Node.PROCESS_MODE_DISABLED], pausing simulation.
##
## The lobby remains in the scene tree and can be woken with [method activate_lobby].
func freeze_lobby(name: StringName) -> void:
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby:
		NetLog.warn(func(): push_warning("MultiplayerLobbyManager: Cannot freeze lobby '%s': not active." % name))
		return
	lobby.level.process_mode = Node.PROCESS_MODE_DISABLED
	NetLog.info("Lobby '%s' frozen." % name)


## Removes and frees the lobby, releasing its memory.
##
## The lobby is detached from the tree synchronously before being freed, which triggers
## [signal lobby_despawned] and removes it from [member active_lobbies] immediately.
func destroy_lobby(name: StringName) -> void:
	var lobby := active_lobbies.get(name) as Lobby
	if not lobby:
		NetLog.warn(func(): push_warning("MultiplayerLobbyManager: Cannot destroy lobby '%s': not active." % name))
		return
	NetLog.info("Destroying lobby '%s'." % name)
	if lobby.get_parent():
		lobby.get_parent().remove_child(lobby)
	lobby.queue_free()


## Instantiates all lobbies configured as [constant LoadMode.ON_STARTUP].
##
## Lobbies configured as [constant LoadMode.ON_DEMAND] are skipped to save startup time and RAM.
## Called automatically after [signal configured] is received.
## [br][br]
## When [member level_spawn_function] is set this method is a no-op; call [method spawn]
## manually in a [signal configured] handler for lobbies that should exist at startup.
func spawn_lobbies() -> void:
	NetLog.trace("MultiplayerLobbyManager: spawn_lobbies called.")
	if not multiplayer.is_server():
		return
	_build_lobby_paths()
	if _lobby_paths.is_empty():
		NetLog.warn(func(): push_warning("MultiplayerLobbyManager: No lobby scenes are registered."))
		return
	NetLog.info("Checking %d lobby/lobbies for ON_STARTUP." % _lobby_paths.size())
	for name: StringName in _lobby_paths:
		if _get_config(name)["load_mode"] == LoadMode.ON_STARTUP:
			spawn_lobby(name)


## Spawn function used by [MultiplayerSpawner] to wrap a level scene in a [Lobby] container.
##
## If [member level_spawn_function] is set, delegates level instantiation to that callable so
## the level can be initialised with arbitrary data before entering the tree. Otherwise
## [param data] must be a [String] file path; a cached scene from [member _lobby_cache] is
## consumed if available, otherwise the scene is loaded from disk.
## Selects the server or client [Lobby] variant based on the current peer role.
func _spawn_lobby_node(data: Variant) -> Node:
	var level: Node

	if level_spawn_function.is_valid():
		level = level_spawn_function.call(data)
		if not is_instance_valid(level):
			NetLog.error(func(): push_error("MultiplayerLobbyManager: level_spawn_function returned null."))
			return null
	elif data is String:
		var level_file_path: String = data
		NetLog.info("Instantiating lobby node for: %s" % level_file_path)
		var level_scene: PackedScene
		if _lobby_cache.has(level_file_path):
			level_scene = _lobby_cache[level_file_path]
			_lobby_cache.erase(level_file_path)
		else:
			level_scene = load(level_file_path)
		level = level_scene.instantiate()
	else:
		NetLog.error(func(): push_error("MultiplayerLobbyManager: invalid spawn data and no level_spawn_function set."))
		return null

	var lobby_scene: PackedScene = (SERVER_LOBBY
		if multiplayer.is_server() else CLIENT_LOBBY)
	var lobby: Lobby = lobby_scene.instantiate()

	lobby.level = level
	lobby.tree_entered.connect(lobby_spawned.emit.bind(lobby))
	lobby.tree_exited.connect(lobby_despawned.emit.bind(lobby))

	return lobby


## RPC called by the client to request entry into a lobby after connecting.
##
## Deserializes [param client_data_bytes] into a [MultiplayerClientData], activates the
## destination lobby on demand if required, then emits [signal ClientComponent.player_joined].
@rpc("any_peer", "call_remote", "reliable")
func request_join_player(client_data_bytes: PackedByteArray) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	NetLog.info("Received join request from peer %d." % peer_id)

	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(client_data_bytes)
	client_data.peer_id = peer_id

	NetLog.debug("Join request data: username=%s spawner=%s" % [client_data.username, client_data.spawner_path.node_path])

	var lobby_name := StringName(client_data.spawner_path.get_scene_name())
	await activate_lobby(lobby_name)

	var lobby := active_lobbies.get(lobby_name) as Lobby
	if not lobby:
		NetLog.error(func(): push_error("res://res://addons/networked/utils/net_log.gd:54 Join request failed: Lobby '%s' could not be activated." % lobby_name))
		return

	var spawner_client: ClientComponent = (
		lobby.level.get_node_or_null(client_data.spawner_path.node_path))
	assert(spawner_client, "Player to be connected needs to have a `ClientComponent`.")
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
	NetLog.info("Lobby spawned: %s" % lobby.level.name)
	active_lobbies[lobby.level.name] = lobby
	if multiplayer.is_server():
		lobby.synchronizer.despawned.connect(
			_on_player_left_lobby.bind(StringName(lobby.level.name)))
		_apply_empty_action_if_needed.call_deferred(StringName(lobby.level.name))


func _on_player_left_lobby(_player: Node, lobby_name: StringName) -> void:
	NetLog.debug("Player left lobby '%s'. Evaluating empty action." % lobby_name)
	_apply_empty_action_if_needed(lobby_name)


func _on_lobby_despawned(node: Node) -> void:
	var lobby := node as Lobby
	NetLog.info("Lobby despawned: %s" % lobby.level.name)
	active_lobbies.erase(lobby.level.name)


func _on_server_disconnected() -> void:
	NetLog.info("Server disconnected. Cleaning up lobbies.")
	for lobby: Lobby in active_lobbies.values():
		if lobby.is_inside_tree():
			lobby.get_parent().remove_child(lobby)
		lobby.queue_free()


func _on_configured() -> void:
	NetLog.trace("MultiplayerLobbyManager: _on_configured called.")
	if not multiplayer.server_disconnected.is_connected(_on_server_disconnected):
		multiplayer.server_disconnected.connect(_on_server_disconnected)

	if multiplayer.is_server():
		var debug_viewports: ViewportDebug = VIEWPORTS_DEBUG.instantiate()
		child_entered_tree.connect(debug_viewports._on_node_entered)
		child_exiting_tree.connect(debug_viewports._on_node_exited)
		add_child(debug_viewports)

		spawn_lobbies.call_deferred()


func _build_lobby_paths() -> void:
	if not _lobby_paths.is_empty():
		return
	for path: String in lobbies:
		var basename := StringName(path.get_file().get_basename())
		_lobby_paths[basename] = path
		NetLog.debug("Registered lobby path: '%s' → '%s'." % [basename, path])
