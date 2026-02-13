class_name NetworkedConfig
extends Resource

@export_file var clients: Array[String]
@export_file var levels: Array[String]

@export var client_backend: MultiplayerClientBackend
@export var server_backend: MultiplayerServerBackend


func validate_web() -> void:
	push_warning("Validating for web.")
	if OS.has_feature("web"):
		push_warning("Changing backends to WebRTC loopback.")
		client_backend = WebRTCLoopbackClientBackend.new()
		server_backend = WebRTCLoopbackServerBackend.new()
