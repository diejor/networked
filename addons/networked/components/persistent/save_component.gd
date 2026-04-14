## Handles saving, loading, and network synchronization of a player's persistent state.
##
## Attach this node (with unique name [code]%SaveComponent[/code]) to your player scene alongside
## a [SaveContainer] and a [SaveSynchronizer]. On spawn, [method spawn] will load the player's
## last saved state from the [member database], falling back to the spawner's current state on first play.
## All registered components are saved automatically on graceful shutdown.
class_name SaveComponent
extends NetComponent

## Emitted after [method instantiate] completes and the synchronizer is ready.
signal instantiated
## Emitted after a save state is loaded (even if the record was not found).
signal loaded
## Emitted when the state is saved or pushed over the network.
signal state_changed(caller: Node)
## Emitted each time a client-owned [MultiplayerSynchronizer] delivers a delta update.
signal client_synchronized


var _save_cid: StringName = &""


## Per-peer storage bucket for [SaveComponent].
##
## All instances belonging to the same peer share one bucket via [PeerContext].
class Bucket extends RefCounted:
	var registered: Array[SaveComponent] = []
	var shutting_down: bool = false


## The [SaveContainer] resource that holds the serializable state for this entity.
@export var save_container: SaveContainer

## The [NetworkedDatabase] to read/write this entity's state.
## The resource must be saved as a [code].tres[/code] file (not embedded) to avoid
## resource-path conflicts across scenes.
@export var database: NetworkedDatabase

## The table name used when reading/writing to [member database].
@export var table_name: StringName

var save_synchronizer: SaveSynchronizer:
	get:
		if not save_synchronizer:
			save_synchronizer = SaveSynchronizer.new(self)
		return save_synchronizer

func _enter_tree() -> void:
	var _force_init = save_synchronizer

func _init() -> void:
	## TODO: move name conventions to NetComponent
	name = "SpawnComponent"
	unique_name_in_owner = true

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


## Returns an editor warning if the database configuration is incomplete.
func _get_configuration_warnings() -> PackedStringArray:
	var warnings: PackedStringArray = []
	if not database:
		warnings.append("'database' must be assigned for persistence.")
	else:
		if table_name.is_empty():
			warnings.append("'table_name' must be set when 'database' is assigned.")
		if database.resource_path.is_empty():
			warnings.append(
				"'database' is an embedded resource. Save it as a .tres file to avoid "
				+ "resource-path conflicts and potential file-lock issues."
			)
	return warnings


## Returns the stable entity identifier used as the database record ID.
##
## Prefers [member ClientComponent.username] when a [ClientComponent] sibling is
## present, otherwise falls back to [member Node.name] of the owner.
func _get_entity_id() -> StringName:
	if not owner:
		return &""
	var client: ClientComponent = owner.get_node_or_null("%ClientComponent")
	if client and not client.username.is_empty():
		return StringName(client.username)
	return StringName(owner.name)


## Writes the current state of the container to the [member database].
func save_state() -> Error:
	if not database or table_name.is_empty():
		log_warn("SaveComponent: cannot save state; database or table_name is missing.")
		return ERR_UNCONFIGURED

	var entity_id := _get_entity_id()
	var data := _container_to_dict()

	_emit_debug_event(&"db.upsert_begin", {table = table_name, id = entity_id})
	var db_err := database.transaction(func(tx: NetworkedDatabase.TransactionContext):
		tx.queue_upsert(table_name, entity_id, data)
	)

	if db_err == OK:
		log_info("SaveComponent: state saved to database (table=%s, id=%s)." % [table_name, entity_id])
		_emit_debug_event(&"db.upserted", {table = table_name, id = entity_id})
	else:
		log_warn("SaveComponent: database upsert failed. Error: %s" % error_string(db_err))
		_emit_debug_event(&"db.upsert_failed", {table = table_name, id = entity_id, error = db_err})

	return db_err


## Loads the saved state from the database and immediately pushes it to the scene.
func load_state() -> Error:
	if not database or table_name.is_empty():
		log_warn("SaveComponent: cannot load state; database or table_name is missing.")
		loaded.emit()
		return ERR_UNCONFIGURED

	var entity_id := _get_entity_id()
	log_trace("SaveComponent: Loading state from database (table=%s, id=%s)" % [table_name, entity_id])
	_emit_debug_event(&"db.load_begin", {table = table_name, id = entity_id})

	var out_error: Array[int] = [OK]
	var record: Dictionary = database.find_by_id(table_name, entity_id, out_error)

	if out_error[0] == ERR_FILE_NOT_FOUND:
		# PURGE policy: DB record was deleted. Treat this as a first-play scenario.
		_emit_debug_event(&"db.load_mismatch_purge", {table = table_name, id = entity_id})
		loaded.emit()
		return ERR_FILE_NOT_FOUND

	if out_error[0] == ERR_UNCONFIGURED:
		# FAIL policy: developer opted in to explicit mismatch errors.
		_emit_debug_event(&"db.load_mismatch_fail", {table = table_name, id = entity_id})
		loaded.emit()
		return ERR_UNCONFIGURED

	if record.is_empty():
		log_debug("No database record found for (table=%s, id=%s)." % [table_name, entity_id])
		_emit_debug_event(&"db.load_miss", {table = table_name, id = entity_id})
		loaded.emit()
		return ERR_FILE_NOT_FOUND

	_emit_debug_event(&"db.loaded", {table = table_name, id = entity_id})
	_apply_dict_to_container(record)
	push_to_scene()
	log_info("State loaded from database (table=%s, id=%s)." % [table_name, entity_id])
	loaded.emit()
	return OK


## Deserializes a network byte array into the container and pushes the result to the scene.
func deserialize_scene(bytes: PackedByteArray) -> void:
	log_trace("SaveComponent: Deserializing scene (Size: %d)" % bytes.size())
	save_container.deserialize(bytes)
	push_to_scene()


## Pulls the latest data from the scene and serializes it into a network-ready byte array.
func serialize_scene() -> PackedByteArray:
	log_trace("SaveComponent: Serializing scene.")
	pull_from_scene()
	return save_container.serialize()


## Pushes the loaded container data into the active scene nodes.
func push_to_scene() -> Error:
	log_trace("SaveComponent: Pushing container to scene.")
	var push_err: Error = save_synchronizer.push_to_scene()

	match push_err:
		ERR_UNCONFIGURED:
			# If the scene nodes don't match the container, purge the record.
			if database and not table_name.is_empty():
				var entity_id := _get_entity_id()
				database.delete(table_name, entity_id)
				_emit_debug_event(&"db.purged", {table = table_name, id = entity_id})
			return push_err
		OK:
			return push_err
		_:
			log_error("Unexpected error during push: %s." % error_string(push_err))
			return push_err


## Updates the save container with the current live values from the scene nodes.
func pull_from_scene() -> void:
	save_synchronizer.pull_from_scene()


## Sends the current save state over the network to a specific peer.
func push_to(peer_id: int) -> void:
	_emit_debug_event(&"save.push_to", {peer_id = peer_id})
	save_synchronizer.push_to(peer_id)


## Handles a state change event by saving to database and emitting network sync signals.
func on_state_changed() -> void:
	save_state()
	state_changed.emit()
	client_synchronized.emit()


## Initializes the underlying save synchronizer and registers this entity's schema
## with the database.
func instantiate() -> void:
	save_synchronizer.setup()
	assert(save_synchronizer._initialized)

	if database and not table_name.is_empty():
		var columns: Array[StringName] = save_synchronizer._get_tracked_property_names()
		database.register_schema(table_name, columns)
		_emit_debug_event(&"db.schema_registered", {table = table_name, columns = columns})

	instantiated.emit()


## Initializes the synchronizer and loads saved state from the database.
##
## If no record is found, copies the state from [param caller]'s [SaveComponent] instead.
## [param caller] is typically the spawner node.
func spawn(caller: Node) -> void:
	_save_cid = StringName("save_%d" % Time.get_ticks_usec())
	_emit_debug_event(&"save.spawn_begin", {}, _save_cid)
	instantiate()
	_emit_debug_event(&"save.instantiated", {}, _save_cid)

	var load_err: Error = load_state()
	_emit_debug_event(&"save.loaded", {found = (load_err == OK)}, _save_cid)
	assert(load_err == OK or load_err == ERR_FILE_NOT_FOUND or load_err == ERR_UNCONFIGURED,
		"Something failed while trying to load player. Error: %s." % error_string(load_err))

	if load_err == ERR_FILE_NOT_FOUND:
		var spawner_save: SaveComponent = caller.get_node_or_null("%SaveComponent")
		if spawner_save and spawner_save.save_synchronizer._initialized:
			log_debug("Loading data from spawner.")
			deserialize_scene(spawner_save.serialize_scene())


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		var bucket := _get_bucket()
		if bucket and not bucket.shutting_down:
			bucket.shutting_down = true
			_handle_shutdown()


## Static entry point: saves all components registered in [param ctx].
## Use this when no [SaveComponent] instance is available (e.g. [NetworkSession]).
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
	log_info("Beginning graceful shutdown...")
	SaveComponent.save_all_in(get_peer_context())
	log_info("All states saved. Quitting.")
	(Engine.get_main_loop() as SceneTree).quit()


# ── Database helpers ──────────────────────────────────────────────────────────

func _container_to_dict() -> Dictionary:
	var dict: Dictionary = {}
	for key in save_container:
		dict[StringName(key)] = save_container.get_value(StringName(key))
	return dict


func _apply_dict_to_container(data: Dictionary) -> void:
	for key: StringName in data:
		save_container.set_value(key, data[key])
