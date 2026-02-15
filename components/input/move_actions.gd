class_name PlayerActions
extends ActionsResource

@export_custom(PROPERTY_HINT_INPUT_NAME, &"action") var move_left: StringName
@export_custom(PROPERTY_HINT_INPUT_NAME, &"action") var move_right: StringName
@export_custom(PROPERTY_HINT_INPUT_NAME, &"action") var move_up: StringName
@export_custom(PROPERTY_HINT_INPUT_NAME, &"action") var move_down: StringName
@export_custom(PROPERTY_HINT_INPUT_NAME, &"action") var sprint: StringName

func get_actions() -> Array:
	var filter := func(prop: Dictionary) -> bool:
		return prop.hint_string == &"action"
	var map := func (prop: Dictionary) -> StringName:
		return prop.name
	return get_property_list().filter(filter).map(map)
		
