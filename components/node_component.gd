@tool
class_name NodeComponent
extends Node

signal client_synchronized

@export var update_replication_config: bool = false:
	set(value):
		if Engine.is_editor_hint():
			_auto_configure_replication()
			update_configuration_warnings()

var api: SceneMultiplayer:
	get: return multiplayer

var lobby_manager: MultiplayerLobbyManager:
	get:
		if multiplayer:
			return get_node(api.root_path)
		return null

var lobby: Lobby:
	get:
		if owner and owner.owner:
			return owner.owner.owner
		return null

var tp_layer: TPLayerAPI:
	get:
		if not is_inside_tree():
			return null
		if not multiplayer.is_server():
			return lobby_manager.tp_layer
		return null


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	if owner and get_client_synchronizers().is_empty():
		warnings.append("Requires at least one MultiplayerSynchronizer in the scene with root_path pointing to the owner.")
	return warnings


func _ready() -> void:
	if Engine.is_editor_hint():
		call_deferred("_auto_configure_replication")
		call_deferred("update_configuration_warnings")
		return
		
	assert(get_client_synchronizers().size() > 0, 
		"Networked addon needs at least one `MultiplayerSynchronizer` with \
`root_path = %s` added to `%s` scene for its components to work. Please restart \
the scene after adding the synchronizer." % [owner.name, owner.name])
	
	_assert_replicated_properties()
	
	for sync in get_synchronizers():
		sync.delta_synchronized.connect(client_synchronized.emit)


func sync_only_server() -> void:
	for sync in get_synchronizers():
		sync.set_visibility_for(0, false)
		sync.set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)
		sync.update_visibility()


func get_client_synchronizers() -> Array[MultiplayerSynchronizer]:
	return get_synchronizers().filter(func(sync: MultiplayerSynchronizer):
		return sync.owner == owner
	)


func get_synchronizers() -> Array[MultiplayerSynchronizer]:
	var synchronizers: Array[MultiplayerSynchronizer] = []
	if not owner:
		return synchronizers
		
	synchronizers.assign(owner.find_children("*", "MultiplayerSynchronizer"))
	return synchronizers.filter(func(sync: MultiplayerSynchronizer):
		return sync.get_node(sync.root_path) == owner
	)


func _assert_replicated_properties() -> void:
	var synchronizers := get_client_synchronizers()
	var current_path: NodePath = owner.get_path_to(self)
	
	for property in _get_replicated_properties():
		var prop_path := NodePath(String(current_path) + ":" + property.name)
		var hint: String = property.hint_string
		var is_valid := false
		
		for sync in synchronizers:
			var config := sync.replication_config
			if config and config.has_property(prop_path):
				var mode := config.property_get_replication_mode(prop_path)
				
				is_valid = (
					(hint == "replicated:always" and mode == SceneReplicationConfig.REPLICATION_MODE_ALWAYS) or
					(hint == "replicated:on_change" and mode == SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE) or
					(hint == "replicated:never" and mode == SceneReplicationConfig.REPLICATION_MODE_NEVER) or
					(hint == "replicated" and mode != SceneReplicationConfig.REPLICATION_MODE_NEVER)
				)
				
				if is_valid:
					break
					
		assert(is_valid, "`%s` property `%s` lacks proper replication config. Reload the scene in the editor to auto-configure." % [self.name, prop_path])


func _auto_configure_replication() -> void:
	if not owner:
		return
		
	var synchronizers := get_client_synchronizers()
	if synchronizers.is_empty():
		return
		
	var target_sync := synchronizers[0]
	if not target_sync.replication_config:
		target_sync.replication_config = SceneReplicationConfig.new()
		
	var current_path: NodePath = owner.get_path_to(self)
	var replicated_properties := _get_replicated_properties()
	
	for replicated in replicated_properties:
		_configure_property(replicated, current_path, synchronizers, target_sync)


func _get_replicated_properties() -> Array[Dictionary]:
	var properties: Array[Dictionary] = []
	properties.assign(get_property_list().filter(
		func(property: Dictionary) -> bool:
			return property.hint_string.begins_with("replicated")
	))
	return properties


func _configure_property(property_data: Dictionary, base_path: NodePath, synchronizers: Array[MultiplayerSynchronizer], fallback_sync: MultiplayerSynchronizer) -> void:
	var prop_path := NodePath(String(base_path) + ":" + property_data.name)
	var hint: String = property_data.hint_string
	var active_config: SceneReplicationConfig = null
	
	for sync in synchronizers:
		if sync.replication_config and sync.replication_config.has_property(prop_path):
			active_config = sync.replication_config
			break
			
	if not active_config:
		active_config = fallback_sync.replication_config
		active_config.add_property(prop_path)
		active_config.property_set_spawn(prop_path, true)
		
	_apply_replication_mode(active_config, prop_path, hint)


func _apply_replication_mode(config: SceneReplicationConfig, path: NodePath, hint: String) -> void:
	if hint == "replicated:always":
		config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	elif hint == "replicated:on_change":
		config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	elif hint == "replicated:never":
		config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_NEVER)
	elif hint == "replicated":
		if config.property_get_replication_mode(path) == SceneReplicationConfig.REPLICATION_MODE_NEVER:
			config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
