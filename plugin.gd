@tool
extends EditorPlugin


const AUTOLOAD_CLIENT_PATH = "res://addons/networked/core/client/Client.tscn"
const AUTOLOAD_SERVER_PATH = "res://addons/networked/core/server/Server.tscn"
const AUTOLOAD_SCENE_MANAGER_PATH = "res://addons/networked/components/scene/scene_manager_autoload.gd"

func _enter_tree():
	add_autoload_singleton("Client", AUTOLOAD_CLIENT_PATH)
	add_autoload_singleton("Server", AUTOLOAD_SERVER_PATH)
	add_autoload_singleton("SceneManager", AUTOLOAD_SCENE_MANAGER_PATH)

func _exit_tree():
	remove_autoload_singleton("Client")
	remove_autoload_singleton("Server")
	remove_autoload_singleton("SceneManager")
