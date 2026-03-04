class_name SpawnSynchronizer
extends MultiplayerSynchronizer

@export var client: NodeComponent:
	set(node):
		client = node

func _enter_tree() -> void:
	root_path = get_path_to(client.owner)
	_config_spawn_properties()
	set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)

func _config_spawn_properties() -> void:
	if replication_config.get_properties().size() > 0:
		return
	var new_config := SceneReplicationConfig.new()
	var synchronizers: Array[MultiplayerSynchronizer] = client.get_synchronizers()
	
	for sync: MultiplayerSynchronizer in synchronizers:
		if sync == self or not sync.replication_config:
			continue
			
		var source_config: SceneReplicationConfig = sync.replication_config
		for property: NodePath in source_config.get_properties():
			if not new_config.has_property(property):
				new_config.add_property(property)
				new_config.property_set_replication_mode(property, SceneReplicationConfig.REPLICATION_MODE_NEVER)
				new_config.property_set_spawn(property, true)
				new_config.property_set_sync(property, false)
				new_config.property_set_watch(property, false)
	
	replication_config = new_config
