class_name NodeComponent
extends Node

@onready var state_sync: StateSynchronizer = %StateSynchronizer

var api: SceneMultiplayer:
	get: return multiplayer as SceneMultiplayer
var lobby_manager: LobbyManager:
	get: return get_node((multiplayer as SceneMultiplayer).root_path)

var transition_player: TransitionPlayer:
	get:
		if not multiplayer.is_server():
			return lobby_manager.get_node("%TransitionAnim")
		return null

var transition_progress: TextureProgressBar:
	get: 
		if not multiplayer.is_server():
			return lobby_manager.get_node("%TransitionProgress")
		return null


var lobby: Lobby:
	get:
		if owner and owner.owner:
			return owner.owner.owner
		return null


func assert_replicated() -> bool:
	var property_list := get_property_list()
	var replicated_properties := property_list.filter(
	func(property: Dictionary) -> bool:
		if property.hint_string == "replicated":
			return true
		return false)
	
	var config := state_sync.replication_config
	var current_path: NodePath = owner.get_path_to(self)
	for replicated: Dictionary in replicated_properties:
		var prop_path: NodePath = String(current_path) + ":" + replicated.name
		
		assert(config.has_property(prop_path), 
			"Component `%s` depends on property `%s` to be replicated by `%s`. \
Add it through the editor by configuring `%s` replication config.\
			" %[self.name, prop_path, state_sync.name, state_sync.name])
	
	return true


func _ready() -> void:
	assert(assert_replicated())
