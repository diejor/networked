class_name NodeComponent
extends Node

@onready var state_sync: StateSynchronizer = %StateSynchronizer


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
