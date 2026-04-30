@tool
## Handles saving, loading, and network synchronization of an entity's persistent state.
##
## Attach this node (with unique name [code]%SaveComponent[/code]) to your player scene.
## Assign [member database] and [member table_name] to define where data is persisted.
## On spawn, [method spawn] loads saved state from the database, falling back to the
## spawner's current state on first play.
##
## [b]How it works:[/b]
## [br]- [SaveComponent] extends [ProxySynchronizer].
## [br]- Properties picked in the Editor's Replication panel are automatically virtualized
## at runtime using their leaf name (e.g., [code].:position[/code] becomes [code]&"position"[/code]).
## [br]- Reads are forwarded to the live scene node; writes land in [member bound_entity]
## and are flushed to the database once per frame via [method _save_once].
##
## [b]Declaring Persistent Properties:[/b]
## [br]1. Select the [SaveComponent] node in the Editor.
## [br]2. In the "Replication" bottom panel, set "Root Node" to your player root (e.g. [code]..[/code]).
## [br]3. Add properties you want to save (e.g., [code].:position[/code] or [code]Stats:health[/code]).
## [br]4. The component will automatically virtualize them at runtime.
class_name SaveComponent
extends ProxySynchronizer

## Emitted after [method instantiate] completes and the synchronizer is ready.
signal instantiated
## Emitted after a save state is loaded (even if the record was not found).
signal loaded
## Emitted when the state is saved or pushed over the network.
signal state_changed(caller: Node)
## Emitted each time this synchronizer delivers a delta update.
signal client_synchronized


var _save_span: NetSpan
var _dbg: NetwHandle = Netw.dbg.handle(self)
var _initialized: bool = false
var _state_changed: bool = false


# Per-peer storage bucket for SaveComponent.
class Bucket extends RefCounted:
	var registered: Array[SaveComponent] = []
	var shutting_down: bool = false


## The entity whose data is tracked and persisted.
var bound_entity: Entity = DictionaryEntity.new()

## The [NetworkedDatabase] to read/write this entity's state.
@export var database: NetworkedDatabase:
	set(v):
		database = v
		update_configuration_warnings()

## The table name used when reading/writing to [member database].
@export var table_name: StringName:
	set(v):
		table_name = v
		update_configuration_warnings()


func _init() -> void:
	# Keep save-data replication low-frequency — it is not latency-sensitive.
	name = "SaveComponent"
	unique_name_in_owner = true
	delta_interval = 5.0
	replication_interval = 5.0
	visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
	public_visibility = false


func _enter_tree() -> void:
	if not _initialized and not Engine.is_editor_hint():
		_setup_sync()


func _ready() -> void:
	if Engine.is_editor_hint():
		return

	get_tree().set_auto_accept_quit(false)
	_register()
	if not delta_synchronized.is_connected(client_synchronized.emit):
		delta_synchronized.connect(client_synchronized.emit)
	set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)


func _exit_tree() -> void:
	if not Engine.is_editor_hint():
		_unregister()


# ── ProxySynchronizer overrides ────────────────────────────────────────────────

# Reads the live value of path from the scene owner.
func _read_property(_name: StringName, path: NodePath) -> Variant:
	var root: Node = null
	if Engine.is_editor_hint():
		root = get_node_or_null(root_path)
	else:
		root = owner
	
	if not root:
		return null
	
	var node_res := root.get_node_and_resource(path)
	var target: Object = node_res[0]
	var prop_path: NodePath = node_res[2]
	if not target or prop_path.is_empty():
		return null
	return target.get_indexed(prop_path)


# Receives a replicated value from the network and stores it in bound_entity.
func _write_property(name: StringName, _path: NodePath, value: Variant) -> void:
	bound_entity.set_value(name, value)
	_state_changed = true
	_save_once.call_deferred()


# Returns a list of properties for the Editor's property list.
func _get_property_list() -> Array[Dictionary]:
	var properties: Array[Dictionary] = []
	for entity_key: StringName in _properties:
		var value: Variant = bound_entity.get_value(entity_key, null)
		properties.append({"name": entity_key, "type": typeof(value)})
	return properties


# ── Internal sync setup ────────────────────────────────────────────────────────

# Builds the SceneReplicationConfig by virtualizing properties picked in the Editor.
func _setup_sync() -> void:
	if _initialized:
		return
	_initialized = true

	assert(bound_entity, "SaveComponent: bound_entity must not be null.")
	assert(
		bound_entity.resource_local_to_scene,
		"SaveComponent: bound_entity '%s' is not local to scene." % bound_entity,
	)

	if database:
		var path := database.resource_path
		assert(not path.contains("::"),
			"SaveComponent: 'database' must be a saved .tres file, not an embedded resource.")

	finalize()

	for entity_key: StringName in _properties:
		var scene_path: NodePath = _properties[entity_key]
		if scene_path.is_empty():
			continue

		if owner and not bound_entity.has_value(entity_key):
			var value = _read_property(entity_key, scene_path)
			if value != null:
				bound_entity.set_value(entity_key, value)

	notify_property_list_changed()


# Seeds bound_entity from the live scene for any key not yet populated.
func _seed_entity_from_scene() -> void:
	for entity_key: StringName in _properties:
		if not owner or bound_entity.has_value(entity_key):
			continue
		var scene_path: NodePath = _properties[entity_key]
		if scene_path.is_empty():
			continue
		
		var value = _read_property(entity_key, scene_path)
		if value != null:
			bound_entity.set_value(entity_key, value)


# ── Public API ─────────────────────────────────────────────────────────────────

## Returns [code]true[/code] if the component has unsaved changes.
func is_dirty() -> bool:
	return _state_changed


## Sets a value in the [member bound_entity] and flags the component as dirty.
func set_value(key: StringName, value: Variant) -> void:
	if not has_virtual_property(key):
		_dbg.warn("Setting value for untracked key: [code]%s[/code]", [key])
	
	bound_entity.set_value(key, value)
	_state_changed = true
	_save_once.call_deferred()


## Returns the value for [param key] from the [member bound_entity].
func get_value(key: StringName, default: Variant = null) -> Variant:
	return bound_entity.get_value(key, default)


## Returns [code]true[/code] if a record for this entity exists in the database.
func exists_in_db() -> bool:
	if not database or table_name.is_empty():
		return false
	var entity_id := _get_entity_id()
	var out_error: Array[int] = [OK]
	database.find_by_id(table_name, entity_id, out_error)
	return out_error[0] == OK


# ── Scene ↔ entity transfer ────────────────────────────────────────────────────

## Writes entity values for all tracked properties into the live scene nodes.
func push_to_scene() -> Error:
	if not _initialized:
		_dbg.error("push_to_scene called before setup().", func(m): push_error(m))
		return ERR_UNCONFIGURED
	assert(bound_entity)
	for entity_key: StringName in _properties:
		if not bound_entity.has_value(entity_key):
			continue
		_write_scene(entity_key, bound_entity.get_value(entity_key))
	return OK


## Reads live values from the scene into [member bound_entity].
func pull_from_scene() -> void:
	assert(_initialized, "SaveComponent: pull_from_scene called before setup().")
	for entity_key: StringName in _properties:
		var value := _read_property(entity_key, _properties[entity_key])
		if value != null:
			bound_entity.set_value(entity_key, value)


# Writes value for entity_key directly into the live scene node.
func _write_scene(entity_key: StringName, value: Variant) -> void:
	var root: Node = null
	if Engine.is_editor_hint():
		root = get_node_or_null(root_path)
	else:
		root = owner
	
	if not root:
		return
		
	var path := _properties.get(entity_key, NodePath(""))
	if path.is_empty():
		return
	var node_res := root.get_node_and_resource(path)
	var target: Object = node_res[0]
	var prop_path: NodePath = node_res[2]
	if target and not prop_path.is_empty():
		target.set_indexed(prop_path, value)


# ── Deferred dirty coalescing ──────────────────────────────────────────────────

# Emits state_changed once per frame when any property was written.
func _save_once() -> void:
	if _state_changed:
		_on_state_changed()
		_state_changed = false


# ── Network transfer ───────────────────────────────────────────────────────────

## Packages the current scene state and sends it to [param peer_id] over the network.
func push_to(peer_id: int) -> void:
	pull_from_scene()
	_request_push.rpc_id(peer_id, bound_entity.serialize())


# RPC called by a client to push its serialized entity state to this peer.
@rpc("any_peer", "call_remote", "reliable")
func _request_push(bytes: PackedByteArray) -> void:
	bound_entity.deserialize(bytes)
	push_to_scene()
	_on_state_changed()


# ── Database persistence ───────────────────────────────────────────────────────

# Returns the stable entity identifier used as the database record ID.
func _get_entity_id() -> StringName:
	var root: Node = null
	if Engine.is_editor_hint():
		root = get_node_or_null(root_path)
	else:
		root = owner
		
	if not root:
		return &""
		
	var client: SpawnerComponent = root.get_node_or_null("%SpawnerComponent")
	if client and not client.username.is_empty():
		return StringName(client.username)
	return StringName(root.name)


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


# Deserializes a network byte array into the entity and pushes the result to the scene.
func _deserialize_scene(bytes: PackedByteArray) -> void:
	_dbg.trace("deserializing scene (%d bytes)." % [bytes.size()])
	bound_entity.deserialize(bytes)
	push_to_scene()


# Pulls the latest data from the scene and serializes the entity for network transfer.
func _serialize_scene() -> PackedByteArray:
	_dbg.trace("serializing scene.")
	pull_from_scene()
	return bound_entity.serialize()


# ── Lifecycle ──────────────────────────────────────────────────────────────────

# Handles a state change event: saves to database and emits network sync signals.
func _on_state_changed() -> void:
	save_state()
	state_changed.emit()
	client_synchronized.emit()


## Initializes the synchronizer and registers the entity schema with [member database].
func instantiate() -> void:
	if _save_span:
		_save_span.step("instantiate_begin", {
			in_tree = is_inside_tree(),
			tracked_count = _properties.size(),
		})

	_setup_sync()
	_seed_entity_from_scene()
	assert(_initialized)

	if database and not table_name.is_empty():
		var columns: Array[StringName] = _properties.keys()
		database.register_schema(table_name, columns)
		if _save_span:
			_save_span.step("schema_registered", {table = table_name, columns = columns})

	instantiated.emit()


## Initializes the synchronizer and loads saved state.
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
		if spawner_save and spawner_save._initialized:
			_dbg.debug("Loading data from spawner.")
			_deserialize_scene(spawner_save._serialize_scene())

	if local_span:
		local_span.end()


func _notification(what: int) -> void:
	if not Engine.is_editor_hint() and what == NOTIFICATION_WM_CLOSE_REQUEST:
		var bucket := _get_bucket()
		if bucket and not bucket.shutting_down:
			bucket.shutting_down = true
			_handle_shutdown()


# Returns editor warnings when the configuration is incomplete.
func _get_configuration_warnings() -> PackedStringArray:
	var warnings: PackedStringArray = []
	
	if not database:
		warnings.append("'database' must be assigned for persistence.")
	else:
		var path := database.resource_path
		if path.is_empty() or path.contains("::"):
			warnings.append(
				"'database' is an embedded resource. Save it as a .tres file to avoid "
				+ "resource-path conflicts and data loss."
			)
			
		if table_name.is_empty():
			warnings.append("'table_name' must be set when 'database' is assigned.")
		elif database.has_method("has_table") and not database.has_table(table_name):
			warnings.append(
				"Table [code]%s[/code] is not yet registered. " % [table_name] + \
				"Run the game once to register the schema and enable autocomplete."
			)

	var config := replication_config
	if not config or config.get_properties().is_empty():
		warnings.append("No properties are tracked. Pick properties in the Replication panel.")
	else:
		var root := get_node_or_null(root_path)
		if root:
			for prop in config.get_properties():
				var res := root.get_node_and_resource(prop)
				if not res[0] or res[2].is_empty():
					warnings.append("Property [code]%s[/code] not found on target node." % [str(prop)])
	
	return warnings


# ── Static helpers ─────────────────────────────────────────────────────────────

## Static entry point: saves all components registered in [param ctx].
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


# ── Session / bucket access ────────────────────────────────────────────────────

func get_peer_context(peer_id: int = -1) -> PeerContext:
	if peer_id == -1:
		if not is_inside_tree() or not multiplayer:
			return null
		peer_id = multiplayer.get_unique_id()
	var mt := MultiplayerTree.resolve(self)
	var s := NetSessionAccess.new(mt) if mt else null
	return s.get_peer_context(peer_id) if s else null


func _get_bucket() -> Bucket:
	var ctx := get_peer_context()
	return ctx.get_bucket(Bucket) as Bucket if ctx else null


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
