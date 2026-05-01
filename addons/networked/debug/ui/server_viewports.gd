## Debug [CanvasLayer] that renders a tab bar allowing in-editor switching between server scene viewports.
##
## Only shown in debug builds with collision hints enabled (see [DebugFeature]).
class_name ViewportDebug
extends CanvasLayer

@onready var lobbies_tab: TabBar = %LobbiesTab

## Stores world references per scene name.
## Each value is a Dictionary with keys [code]world_2d[/code] and [code]world_3d[/code].
var lobbies: Dictionary[StringName, Dictionary]

var client_lobbies: Array:
	get:
		for managers in get_tree().get_nodes_in_group("scene_managers"):
			if not is_instance_valid(managers.multiplayer) or not managers.multiplayer.has_multiplayer_peer():
				continue
			
			if managers.multiplayer.multiplayer_peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
				continue
				
			if not managers.multiplayer.is_server():
				return managers.get_children()
		return []


func _init() -> void:
	DebugFeature.free_if_debug(self)


func _ready() -> void:
	add_lobby("Client", get_tree().root.world_2d, get_tree().root.world_3d)


func add_lobby(lobby_name: StringName, world_2d: World2D, world_3d: World3D) -> void:
	lobbies[lobby_name] = {world_2d = world_2d, world_3d = world_3d}
	update_tab()

func remove_lobby(lobby_name: StringName) -> void:
	lobbies.erase(lobby_name)
	update_tab()

func update_tab() -> void:
	lobbies_tab.clear_tabs()
	for lobby_name in lobbies.keys():
		lobbies_tab.add_tab(lobby_name)

func _on_node_entered(node: Node) -> void:
	if not node is SubViewport:
		return
	
	var viewport: SubViewport = node as SubViewport
	if not viewport.name in lobbies:
		add_lobby(viewport.name, viewport.world_2d, viewport.world_3d)


func _on_node_exited(node: Node) -> void:
	if not node is SubViewport:
		return
	
	var viewport: SubViewport = node as SubViewport
	if viewport.name in lobbies:
		remove_lobby(viewport.name)


func _on_tab_changed(tab: int) -> void:
	if tab < 0:
		return
		
	var lobby_name := lobbies_tab.get_tab_title(tab)
	if not lobby_name == "Client":
		for child in client_lobbies:
			child.process_mode = Node.PROCESS_MODE_DISABLED
			var scene := child as MultiplayerScene
			if scene and is_instance_valid(scene.level):
				scene.level.set("visible", false)
	else:
		for child in client_lobbies:
			child.process_mode = Node.PROCESS_MODE_INHERIT
			var scene := child as MultiplayerScene
			if scene and is_instance_valid(scene.level):
				scene.level.set("visible", true)

	var entry: Dictionary = lobbies[lobby_name]
	get_tree().root.world_2d = entry.world_2d
	if entry.world_3d != null:
		get_tree().root.world_3d = entry.world_3d
	
