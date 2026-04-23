## Handles saving, loading, and network synchronization of an entity's persistent state.
##
## Attach this node (with unique name [code]%SaveComponent[/code]) to your player scene.
## Assign [member bound_entity] and [member tracked_properties] to define what is persisted
## and which scene properties mirror it. On spawn, [method spawn] loads saved state from
## [member database], falling back to the spawner's current state on first play.
##
## [b]Typical server-side setup:[/b]
## [codeblock lang=gdscript]
## func _on_player_joined(player: Node, username: String) -> void:
##     var save_comp: SaveComponent = player.get_node("%SaveComponent")
##     # bound_entity auto-initializes; load_state() inside spawn() populates it from DB.
##     save_comp.spawn(spawner_node)
## [/codeblock]
##
## [b]Declaring tracked properties:[/b]
## [codeblock lang=gdscript]
## # In the Inspector or in code — maps entity data keys to scene NodePaths:
## save_comp.tracked_properties = {
##     &"health":   NodePath("%HealthBar:value"),
##     &"position": NodePath(".:position"),
## }
## [/codeblock]
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


var _save_span: NetSpan
var _dbg: NetwHandle = Netw.dbg.handle(self)


## Per-peer storage bucket for [SaveComponent].
##
## All instances belonging to the same peer share one bucket via [PeerContext].
class Bucket extends RefCounted:
	var registered: Array[SaveComponent] = []
	var shutting_down: bool = false


## The entity whose data is tracked and persisted.
##
## Defaults to a fresh [DictionaryEntity]. [method spawn] populates it automatically
## from [member database] via [method load_state], so no manual assignment is needed
## for the common case.
##
## Assign a pre-fetched entity when you need a typed subclass or want to pre-load
## data before [method spawn] runs:
## [codeblock lang=gdscript]
## # Optional — only needed for custom Entity subclasses:
## save_comp.bound_entity = db.table(&"players").fetch(username)
## save_comp.spawn(spawner)
## [/codeblock]
var bound_entity: Entity = DictionaryEntity.new()

## The [NetworkedDatabase] to read/write this entity's state.
## The resource must be saved as a [code].tres[/code] file (not embedded) to avoid
## resource-path conflicts across scenes.
@export var database: NetworkedDatabase

## The table name used when reading/writing to [member database].
@export var table_name: StringName

## Maps entity data keys to scene [NodePath]s.
##
## Each entry declares one persistent property: the key is the name used in
## [member bound_entity]; the value is the scene-relative path to the node property
## that mirrors it. The [SaveSynchronizer] reads and writes these paths automatically.
## [codeblock lang=gdscript]
## tracked_properties = {
##     &"health":   NodePath("%HealthBar:value"),
##     &"position": NodePath(".:position"),
##     &"rotation": NodePath(".:rotation"),
## }
## [/codeblock]Name
@export var tracked_properties: Dictionary[StringName, NodePath] = {}

var save_synchronizer: SaveSynchronizer:
	get:
		if not save_synchronizer:
			save_synchronizer = SaveSynchronizer.new(self)
		return save_synchronizer

func _enter_tree() -> void:
	var _force_init = save_synchronizer

func _init() -> void:
	name = "SaveComponent"
	unique_name_in_owner = true

func _ready() -> void:
	assert(save_synchronizer)

	get_tree().set_auto_accept_quit(false)
	_register()

	if not Engine.is_editor_hint():
		for sync in SynchronizersCache.get_client_synchronizers(owner):
			if not sync.delta_synchronized.is_connected(client_synchronized.emit):
				sync.delta_synchronized.connect(client_synchronized.emit)


func _exit_tree() -> void:
	_unregister()

func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return

	var sync := save_synchronizer

	if not sync._initialized:
		sync.setup()


## Returns editor warnings when the configuration is incomplete.
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
	if tracked_properties.is_empty():
		warnings.append("'tracked_properties' is empty — no scene properties will be persisted.")
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


## Writes the current entity state to [member database].
func save_state() -> Error:
	if not database or table_name.is_empty():
		_dbg.warn("Cannot save state; database or table_name is missing.", func(m): push_warning(m))
		return ERR_UNCONFIGURED
	var entity_id := _get_entity_id()
	var db_err := database.transaction(func(tx: NetworkedDatabase.TransactionContext) -> void:
		tx.queue_upsert(table_name, entity_id, bound_entity.to_dict())
	)

	if db_err == OK:
		_dbg.trace("State saved (table=%s, id=%s)." % [table_name, entity_id])
	else:
		_dbg.warn("Database upsert failed. Error: %s" % [error_string(db_err)], func(m): push_warning(m))

	return db_err


## Loads saved state from [member database] and pushes it to the scene.
func load_state() -> Error:
	if not database or table_name.is_empty():
		_dbg.warn("Cannot load state; database or table_name is missing.", func(m): push_warning(m))
		loaded.emit()
		return ERR_UNCONFIGURED
	var entity_id := _get_entity_id()
	_dbg.trace("Loading state (table=%s, id=%s)", [table_name, entity_id])

	var out_error: Array[int] = [OK]
	var record: Dictionary = database.find_by_id(table_name, entity_id, out_error)

	if out_error[0] == ERR_FILE_NOT_FOUND:
		loaded.emit()
		return ERR_FILE_NOT_FOUND

	if out_error[0] == ERR_UNCONFIGURED:
		loaded.emit()
		return ERR_UNCONFIGURED

	if record.is_empty():
		_dbg.debug("No record found (table=%s, id=%s)." % [table_name, entity_id])
		loaded.emit()
		return ERR_FILE_NOT_FOUND

	bound_entity.from_dict(record)
	push_to_scene()
	_dbg.info("State loaded (table=%s, id=%s)." % [table_name, entity_id])
	loaded.emit()
	return OK


## Deserializes a network byte array into the entity and pushes the result to the scene.
func deserialize_scene(bytes: PackedByteArray) -> void:
	_dbg.trace("deserializing scene (%d bytes)." % [bytes.size()])
	bound_entity.deserialize(bytes)
	push_to_scene()


## Pulls the latest data from the scene and serializes the entity for network transfer.
func serialize_scene() -> PackedByteArray:
	_dbg.trace("serializing scene.")
	pull_from_scene()
	return bound_entity.serialize()


## Writes the entity's tracked values into the live scene nodes.
func push_to_scene() -> Error:
	_dbg.trace("pushing entity to scene.")
	return save_synchronizer.push_to_scene()


## Reads live values from the scene into the entity.
func pull_from_scene() -> void:
	save_synchronizer.pull_from_scene()


## Sends the current entity state over the network to a specific peer.
func push_to(peer_id: int) -> void:
	save_synchronizer.push_to(peer_id)


## Handles a state change event: saves to database and emits network sync signals.
func on_state_changed() -> void:
	save_state()
	state_changed.emit()
	client_synchronized.emit()


## Initializes the synchronizer and registers the entity schema with [member database].
##
## Called automatically by [method spawn]. Call manually when not using [method spawn]
## (e.g. when spawning without a database or in tests).
func instantiate() -> void:
	if _save_span:
		_save_span.step("instantiate_begin", {
			in_tree = is_inside_tree(),
			tracked_count = tracked_properties.size(),
		})

	save_synchronizer.setup()
	assert(save_synchronizer._initialized)

	if database and not table_name.is_empty():
		var columns: Array[StringName] = tracked_properties.keys()
		database.register_schema(table_name, columns)
		if _save_span:
			_save_span.step("schema_registered", {table = table_name, columns = columns})

	instantiated.emit()


## Initializes the synchronizer, loads saved state, and falls back to [param caller]'s
## state on first play.
##
## [param caller] is typically the spawner node. Its [SaveComponent] provides the
## default state when no database record exists for the new player.
func spawn(caller: Node, parent_span: NetSpan = null) -> void:
	_save_span = parent_span
	var local_span: NetSpan = null
	if not _save_span:
		local_span = _dbg.span("save_spawn")
		_save_span = local_span

	_save_span.step("spawn_begin")
	instantiate()
	_save_span.step("instantiated")

	var load_err: Error = load_state()
	_save_span.step("loaded", {found = (load_err == OK)})
	assert(load_err == OK or load_err == ERR_FILE_NOT_FOUND or load_err == ERR_UNCONFIGURED,
		"Something failed while trying to load player. Error: %s." % error_string(load_err))

	if load_err == ERR_FILE_NOT_FOUND:
		var spawner_save: SaveComponent = caller.get_node_or_null("%SaveComponent")
		if spawner_save and spawner_save.save_synchronizer._initialized:
			_dbg.debug("Loading data from spawner.")
			deserialize_scene(spawner_save.serialize_scene())

	if local_span:
		local_span.end()


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
	_dbg.info("Beginning graceful shutdown...")
	SaveComponent.save_all_in(get_peer_context())
	_dbg.info("All states saved. Quitting.")
	(Engine.get_main_loop() as SceneTree).quit()
