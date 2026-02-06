@tool
extends EditorPlugin


const AUTOLOAD_CLIENT_PATH = "res://addons/networked/core/client/Client.tscn"
const AUTOLOAD_SERVER_PATH = "res://addons/networked/core/server/Server.tscn"

func _enter_tree():
	add_autoload_singleton("Client", AUTOLOAD_CLIENT_PATH)
	add_autoload_singleton("Server", AUTOLOAD_SERVER_PATH)

func _exit_tree():
	remove_autoload_singleton("Client")
	remove_autoload_singleton("Server")
