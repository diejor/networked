extends Node

@export var client_backend: MultiplayerClientBackend
@export var server_backend: MultiplayerServerBackend

func _ready() -> void:
	assert(server_backend, 
		"Assign a `MultiplayerServerBackend` in `Network` Autoload.")
	assert(client_backend, 
		"Assign a `MultiplayerClientBackend` in `Network` Autoload.")
	Client.backend = client_backend
	Server.backend = server_backend
