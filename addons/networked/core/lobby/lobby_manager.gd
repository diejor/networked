@tool
class_name MultiplayerLobbyManager
extends MultiplayerSpawner

## Central authority that spawns and manages multiplayer lobbies for all connected players.
##
## Extends [MultiplayerSpawner] to replicate lobby scenes to clients automatically.
## Add level scenes to the spawn list via the [member add_to_spawn_list] helper property in the inspector,
## then the manager will instantiate a [Lobby] wrapper around each scene on the server.
## [codeblock]
## # Listen for lobbies becoming available:
## lobby_manager.lobby_spawned.connect(func(lobby): print("Lobby ready: ", lobby.level.name))
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
		
	spawn_function = spawn_lobby
	spawn_path = "."
	add_to_group("lobby_managers")


func _exit_tree() -> void:
	if not Engine.is_editor_hint():
		active_lobbies.clear()


## Instantiates a [Lobby] for every scene path in [member lobbies].
##
## Only runs on the server. Called automatically after [signal configured] is received.
func spawn_lobbies() -> void:
	NetLog.trace("MultiplayerLobbyManager: spawn_lobbies called.")
	if multiplayer.is_server():
		if get_spawnable_scene_count() == 0:
			NetLog.warn("MultiplayerLobbyManager: No lobby scenes are registered.")
			return
		
		NetLog.info("Spawning %d lobbies." % lobbies.size())
		NetLog.debug("Lobbies to spawn: %s" % str(lobbies))
		for level_path: String in lobbies:
			spawn(level_path)


## Spawn function used by [MultiplayerSpawner] to wrap a level scene in a [Lobby] container.
##
## Selects the server or client [Lobby] variant based on the current peer role and
## connects lobby lifecycle signals.
func spawn_lobby(level_file_path: String) -> Node:
	NetLog.info("Spawning lobby for level: %s" % level_file_path)
	var level_scene: PackedScene = load(level_file_path)
	var level: Node = level_scene.instantiate()
	
	var lobby_scene: PackedScene = (SERVER_LOBBY 
		if multiplayer.is_server() else CLIENT_LOBBY)
	var lobby: Lobby = lobby_scene.instantiate()
	
	lobby.level = level
	lobby.tree_entered.connect(lobby_spawned.emit.bind(lobby))
	lobby.tree_exited.connect(lobby_despawned.emit.bind(lobby))
	
	return lobby


## RPC called by the client to request entry into a lobby after connecting.
##
## Deserializes [param client_data_bytes] into a [MultiplayerClientData], then emits
## [signal ClientComponent.player_joined] on the appropriate [ClientComponent].
@rpc("any_peer", "call_remote", "reliable")
func request_join_player(client_data_bytes: PackedByteArray) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	NetLog.info("Received join request from peer %d." % peer_id)
	
	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(client_data_bytes)
	client_data.peer_id = peer_id
	
	NetLog.debug("Join request data: username=%s spawner=%s" % [client_data.username, client_data.spawner_path.node_path])
	
	var lobby_name := client_data.spawner_path.get_scene_name()
	if not active_lobbies.has(lobby_name):
		NetLog.error("Join request failed: Lobby '%s' not found." % lobby_name)
		return
		
	var lobby := active_lobbies[lobby_name]
	var spawner_client: ClientComponent = (
		lobby.level.get_node(client_data.spawner_path.node_path))
	
	spawner_client.player_joined.emit(client_data)


func _on_lobby_spawned(node: Node) -> void:
	var lobby := node as Lobby
	NetLog.info("Lobby spawned: %s" % lobby.level.name)
	active_lobbies[lobby.level.name] = lobby


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
