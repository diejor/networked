class_name ViewportDebug
extends CanvasLayer

@onready var lobbies_tab: TabBar = %LobbiesTab

var lobbies: Dictionary[StringName, World2D]

var client_lobbies: Array:
	get:
		for managers in get_tree().get_nodes_in_group("lobby_managers"):
			if not managers.multiplayer.is_server():
				return managers.get_children()
		return []
	

func _ready() -> void:
	add_lobby("Client", get_tree().root.world_2d)


func add_lobby(lobby_name: StringName, lobby: World2D) -> void:
	lobbies[lobby_name] = lobby
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
		add_lobby(viewport.name, viewport.world_2d)


func _on_node_exited(node: Node) -> void:
	if not node is SubViewport:
		return
	
	var viewport: SubViewport = node as SubViewport
	if viewport.name in lobbies:
		remove_lobby(viewport.name)


func _on_tab_changed(tab: int) -> void:
	var lobby_name := lobbies_tab.get_tab_title(tab)
	if not lobby_name == "Client":
		for child in client_lobbies:
			child.process_mode = Node.PROCESS_MODE_DISABLED
			child.visible = false
	else:
		for child in client_lobbies:
			child.process_mode = Node.PROCESS_MODE_INHERIT
			child.visible = true
	get_tree().root.world_2d = lobbies[lobby_name]
	
