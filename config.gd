class_name NetworkedConfig
extends Resource

@export_file var clients: Array[String]
@export_file var levels: Array[String]

@export var client_backend: MultiplayerClientBackend = WebSocketClientBackend.new()
@export var server_backend: MultiplayerServerBackend = WebSocketServerBackend.new()

func _init() -> void:
	ready.call_deferred()


func ready() -> void:
	if OS.has_feature("web"):
		client_backend = WebRTCLoopbackClientBackend.new()
		server_backend = WebRTCLoopbackServerBackend.new()
