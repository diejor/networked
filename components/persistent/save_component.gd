class_name SaveComponent
extends Node


signal instantiated
signal loaded
signal state_changed(caller: Node)


@export_dir var save_dir: String
@export var save_extension: String = ".tdict"
@export var save_container: SaveContainer

var _save_synchronizer: SaveSynchronizer:
	get:
		return get_child(0)

@onready var save_path: String:
	get:
		_prepare_save_dir()
		assert(save_extension.begins_with("."),
			"Save extension should begin with a dot.")
		
		var client: ClientComponent = owner.get_node_or_null("%ClientComponent")
		var base: String
		if client and not client.username.is_empty():
			base = client.username
		else:
			base = owner.name
		
		save_path = save_dir.path_join(base + save_extension)

		assert(save_path.is_absolute_path(),
			"Invalid save to a not valid file path. " + save_path)
		return save_path

func _ready() -> void:
	assert(save_container)
	assert(_save_synchronizer)
	assert(_save_synchronizer.save_container == save_container)
	
	if SaveManager:
		SaveManager.register(self)


func _exit_tree() -> void:
	if SaveManager:
		SaveManager.unregister(self)


func _prepare_save_dir() -> void:
	if not OS.has_feature("editor"):
		save_dir = save_dir.replace("res://", "user://")
	if not DirAccess.dir_exists_absolute(save_dir):
		DirAccess.make_dir_recursive_absolute(save_dir)

# ------------------------
# Public API
# ------------------------


func save_state() -> Error:
	var err: Error = ResourceSaver.save(save_container, save_path)
	assert(err == OK,
		"Failed to save `%s`. Error: %s" % [save_path, error_string(err)])
	return err


func load_state() -> Error:
	if not ResourceLoader.exists(save_path):
		#push_warning("No file found at path: %s" % save_path)
		loaded.emit()
		return ERR_FILE_NOT_FOUND
	
	var saved_container := ResourceLoader.load(
		save_path, 
		"SaveContainer", 
		ResourceLoader.CACHE_MODE_REPLACE)
	
	if saved_container == null:
		push_error("Save located at `%s` is invalid." % save_path)
		return ERR_CANT_OPEN

	save_container = saved_container
	push_to_scene()
	loaded.emit()
	
	return OK


func deserialize_scene(bytes: PackedByteArray) -> void:
	save_container.deserialize(bytes)
	push_to_scene()
	
func serialize_scene() -> PackedByteArray:
	pull_from_scene()
	return save_container.serialize()


func push_to_scene() -> Error:
	var push_err: Error = _save_synchronizer.push_to_scene()
	match push_err:
		ERR_UNCONFIGURED:
			push_error("Removing unconfigured save at `%s`." % save_path)
			DirAccess.remove_absolute(save_path)
			return push_err
		OK:
			return push_err
		_:
			push_error("Unexpected error: %s." % error_string(push_err))
			return push_err
	


func pull_from_scene() -> void:
	_save_synchronizer.pull_from_scene()


func push_to(peer_id: int) -> void:
	_save_synchronizer.push_to(peer_id)
	

func on_state_changed() -> void:
	save_state()
	state_changed.emit()


func instantiate() -> void:
	_save_synchronizer.setup()
	assert(_save_synchronizer._initialized)
	instantiated.emit()


func spawn(caller: Node) -> void:
	instantiate()
	
	var load_err: Error = load_state()
	assert(load_err == OK or load_err == ERR_FILE_NOT_FOUND, 
		"Something failed while trying to load player. 
		Error: %s." % error_string(load_err))
	
	
	if load_err == ERR_FILE_NOT_FOUND:
		push_warning("Loading data from spawner.")
		var spawner_save: SaveComponent = caller.get_node("%SaveComponent")
		deserialize_scene(spawner_save.serialize_scene())
