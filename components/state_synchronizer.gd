class_name StateSynchronizer
extends MultiplayerSynchronizer

@export var spawn_sync: SpawnSynchronizer


func _init() -> void:
	unique_name_in_owner = true


func _ready() -> void:
	assert(root_path == get_path_to(owner))
	
	var client: ClientComponent = owner.get_node_or_null("%ClientComponent")
	if client.username.is_empty():
		client.sync_only_server()
