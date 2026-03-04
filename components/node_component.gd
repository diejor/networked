class_name NodeComponent
extends Node

signal client_synchronized

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


func assert_replicated() -> bool:
	var property_list := get_property_list()
	var replicated_properties := property_list.filter(
	func(property: Dictionary) -> bool:
		if property.hint_string == "replicated":
			return true
		return false)
	
	var synchronizers := get_synchronizers()
	var current_path: NodePath = owner.get_path_to(self)
	
	for replicated: Dictionary in replicated_properties:
		var prop_path: NodePath = String(current_path) + ":" + replicated.name
		var is_replicated := false
		
		for sync in synchronizers:
			if sync.replication_config and sync.replication_config.has_property(prop_path):
				is_replicated = true
				break
				
		assert(is_replicated,
			"`%s` depends on property `%s` to be replicated. \
Add it through the editor by configuring a MultiplayerSynchronizer's replication config.\
			" % [self.name, prop_path])
	
	return true


func _ready() -> void:
	assert(assert_replicated())
	var client_sync: MultiplayerSynchronizer = get_client_synchronizer()
	if client_sync:
		client_sync.delta_synchronized.connect(client_synchronized.emit)


func update_synchronizers() -> void:
	for sync in get_synchronizers():
		sync.update_visibility()


func sync_only_server() -> void:
	for sync in get_synchronizers():
		sync.set_visibility_for(0, false)
		sync.set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)
		sync.update_visibility()


func get_client_synchronizer() -> MultiplayerSynchronizer:
	for sync in get_synchronizers():
		if sync.get_multiplayer_authority() != MultiplayerPeer.TARGET_PEER_SERVER:
			return sync
	return null


func get_synchronizers() -> Array[MultiplayerSynchronizer]:
	var synchronizers: Array[MultiplayerSynchronizer] = []
	synchronizers.assign(owner.find_children("*", "MultiplayerSynchronizer"))
	return synchronizers
