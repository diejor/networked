class_name SaveComponent
extends NetComponent

signal instantiated
signal loaded
signal state_changed(caller: Node)
signal client_synchronized


## Per-peer storage bucket for [SaveComponent].
## All instances belonging to the same peer share one bucket via [PeerContext].
class Bucket extends RefCounted:
	var registered: Array[SaveComponent] = []
	var shutting_down: bool = false


@export_dir var save_dir: String
@export var save_extension: String = ".tres"
@export var save_container: SaveContainer

var save_synchronizer: SaveSynchronizer:
	get: return %SaveSynchronizer


func _ready() -> void:
	assert(save_container)
	assert(save_synchronizer)
	assert(save_synchronizer.save_container == save_container)

	get_tree().set_auto_accept_quit(false)
	_register()

	if not Engine.is_editor_hint():
		for sync in SynchronizersCache.get_client_synchronizers(owner):
			if not sync.delta_synchronized.is_connected(client_synchronized.emit):
				sync.delta_synchronized.connect(client_synchronized.emit)


func _exit_tree() -> void:
	_unregister()


func _prepare_save_dir() -> void:
	if not OS.has_feature("editor"):
		save_dir = save_dir.replace("res://", "user://")
	if not DirAccess.dir_exists_absolute(save_dir):
		DirAccess.make_dir_recursive_absolute(save_dir)


## Resolves and returns the absolute file path for this entity's save file.
func get_save_path() -> String:
	_prepare_save_dir()
	assert(save_extension.begins_with("."), "Save extension should begin with a dot.")

	if not owner:
		return ""

	var client: ClientComponent = owner.get_node_or_null("%ClientComponent")
	var base: String

	if client and not client.username.is_empty():
		base = client.username
	else:
		base = owner.name

	var save_path: String = save_dir.path_join(base + save_extension)
	assert(save_path.is_absolute_path(), "Invalid save to a not valid file path. " + save_path)

	return save_path


## Writes the current state of the container to the disk.
func save_state() -> Error:
	var save_path := get_save_path()
	var err: Error = ResourceSaver.save(save_container, save_path)

	assert(err == OK, "Failed to save `%s`. Error: %s" % [save_path, error_string(err)])
	return err


## Loads the saved state from the disk and immediately pushes it to the scene.
func load_state() -> Error:
	var save_path := get_save_path()
	if not ResourceLoader.exists(save_path):
		loaded.emit()
		return ERR_FILE_NOT_FOUND

	var saved_container := ResourceLoader.load(save_path, "SaveContainer", ResourceLoader.CACHE_MODE_REPLACE)

	if not is_instance_valid(saved_container) or not saved_container is SaveContainer:
		push_error("Save located at `%s` is invalid." % save_path)
		return ERR_CANT_OPEN

	save_container = saved_container
	push_to_scene()
	loaded.emit()

	return OK


## Deserializes a network byte array into the container and pushes the result to the scene.
func deserialize_scene(bytes: PackedByteArray) -> void:
	save_container.deserialize(bytes)
	push_to_scene()


## Pulls the latest data from the scene and serializes it into a network-ready byte array.
func serialize_scene() -> PackedByteArray:
	pull_from_scene()
	return save_container.serialize()


## Pushes the loaded container data into the active scene nodes.
func push_to_scene() -> Error:
	var push_err: Error = save_synchronizer.push_to_scene()

	match push_err:
		ERR_UNCONFIGURED:
			var save_path := get_save_path()
			push_error("Removing unconfigured save at `%s`." % save_path)
			DirAccess.remove_absolute(save_path)
			return push_err
		OK:
			return push_err
		_:
			push_error("Unexpected error: %s." % error_string(push_err))
			return push_err


## Updates the save container with the current live values from the scene nodes.
func pull_from_scene() -> void:
	save_synchronizer.pull_from_scene()


## Sends the current save state over the network to a specific peer.
func push_to(peer_id: int) -> void:
	save_synchronizer.push_to(peer_id)


## Handles a state change event by saving to disk and emitting network sync signals.
func on_state_changed() -> void:
	save_state()
	state_changed.emit()
	client_synchronized.emit()


## Initializes the underlying save synchronizer.
func instantiate() -> void:
	save_synchronizer.setup()
	assert(save_synchronizer._initialized)
	instantiated.emit()


## Bootstraps the entity by loading from disk, or falling back to the caller's data if a save doesn't exist.
func spawn(caller: Node) -> void:
	instantiate()

	var load_err: Error = load_state()
	assert(load_err == OK or load_err == ERR_FILE_NOT_FOUND,
		"Something failed while trying to load player. Error: %s." % error_string(load_err))

	if load_err == ERR_FILE_NOT_FOUND:
		var spawner_save: SaveComponent = caller.get_node_or_null("%SaveComponent")
		if spawner_save and spawner_save.save_synchronizer._initialized:
			deserialize_scene(spawner_save.serialize_scene())


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		var bucket := _get_bucket()
		if bucket and not bucket.shutting_down:
			bucket.shutting_down = true
			_handle_shutdown()


## Saves all components in this peer's bucket where the local peer is authority.
func save_game() -> void:
	SaveComponent.save_all_in(get_peer_context())


## Static entry point: saves all components registered in [param ctx].
## Use this when no [SaveComponent] instance is available (e.g. [MultiplayerNetwork]).
static func save_all_in(ctx: PeerContext) -> void:
	if not ctx:
		return
	var bucket := ctx.get_bucket(Bucket) as Bucket
	if not bucket:
		return
	for component in bucket.registered:
		if not component.is_multiplayer_authority():
			continue
		if component.get_multiplayer_authority() == MultiplayerPeer.TARGET_PEER_SERVER:
			component.save_state()
		else:
			component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)


func _get_bucket() -> Bucket:
	return get_bucket(Bucket) as Bucket


func _register() -> void:
	var bucket := _get_bucket()
	if bucket and not bucket.registered.has(self):
		bucket.registered.append(self)


func _unregister() -> void:
	var bucket := _get_bucket()
	if bucket:
		bucket.registered.erase(self)


func _handle_shutdown() -> void:
	print("Beginning graceful shutdown...")
	SaveComponent.save_all_in(get_peer_context())
	print("All states saved. Quitting.")
	(Engine.get_main_loop() as SceneTree).quit()
