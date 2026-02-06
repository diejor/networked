extends Node

var registered_components: Array[SaveComponent] = []

func _ready() -> void:
	get_tree().set_auto_accept_quit(false)

func register(component: SaveComponent) -> void:
	if not registered_components.has(component):
		registered_components.append(component)

func unregister(component: SaveComponent) -> void:
	registered_components.erase(component)

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		_handle_shutdown()

func _handle_shutdown() -> void:
	print("Beginning graceful shutdown...")
	
	for component in registered_components:
		component.pull_from_scene()
		var err := component.save_state()
		if err != OK:
			push_error("Failed to save component: ", component.owner.name)

	print("All states saved. Quitting.")
	get_tree().quit()
