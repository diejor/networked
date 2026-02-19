class_name SpawnSynchronizer
extends MultiplayerSynchronizer

var state_sync: MultiplayerSynchronizer:
	get: return get_parent()

func _enter_tree() -> void:
	root_path = get_path_to(state_sync.owner)
	_config_spawn_properties(state_sync.replication_config)
	set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)

func _config_spawn_properties(source_config: SceneReplicationConfig) -> void:
	var new_config := SceneReplicationConfig.new()

	for property: NodePath in source_config.get_properties():
		new_config.add_property(property)
		new_config.property_set_replication_mode(property, SceneReplicationConfig.REPLICATION_MODE_NEVER)
		new_config.property_set_spawn(property, true)
		new_config.property_set_sync(property, false)
		new_config.property_set_watch(property, false)
	
	replication_config = new_config
