@tool
## Handles saving, loading, and network synchronization of an entity's persistent state.
##
## [SaveComponent] acts as a bridging layer between the live scene state and a
## [NetwDatabase]. It automatically virtualizes properties selected in the
## Editor's "Replication" panel, synchronizing changes across the network and
## persisting them to the database once per frame.
##
## [b]How to use:[/b]
## [br]1. Attach [SaveComponent] to your player or persistent entity scene.
## [br]2. In the "Replication" panel, set "Root Node" to [code].[/code] (self).
## [br]3. Add properties to save using paths relative to SaveComponent
##     (e.g., [code]..:position[/code] for the owner's position).
## [br]4. Assign a [member database] and [member table_name].
## [br]5. Call [method hydrate] to load existing data into the scene on spawn.
##
## [codeblock]
## # Accessing data programmatically:
## var save := %SaveComponent
## # Set a value (marks dirty and saves deferred):
## save.set_value(&"gold", 500)
## # Get a value from the tracked state:
## var gold = save.get_value(&"gold", 0)
## [/codeblock]
class_name SaveComponent
extends ProxySynchronizer

## Emitted after sync setup completes and the synchronizer is ready.
signal instantiated
## Emitted after a save state is loaded (even if the record was not found).
signal loaded
## Emitted when the state is saved or pushed over the network.
signal state_changed(caller: Node)
## Emitted each time this synchronizer delivers a delta update.
signal client_synchronized
## Emitted when a [method push_to] acknowledgment arrives from the remote peer.
signal push_acknowledged


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

## The [NetwDatabase] to read/write this entity's state.
@export var database: NetwDatabase:
	set(v):
		database = v
		update_configuration_warnings()

## The table name used when reading/writing to [member database].
@export var table_name: StringName:
	set(v):
		table_name = v
		update_configuration_warnings()


func _init() -> void:
	# Keep save-data replication low-frequency - it is not latency-sensitive.
	name = "SaveComponent"
	root_path = "."
	unique_name_in_owner = true
	delta_interval = 5.0
	replication_interval = 5.0
	visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
	public_visibility = false

func _enter_tree() -> void:
	if not _initialized and not Engine.is_editor_hint():
		_instantiate_sync()


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

# Reads the live value of path from the node at [member _target_root].
func _read_property(_name: StringName, path: NodePath) -> Variant:
	var root := get_node_or_null(_target_root)
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
	var root := get_node_or_null(_target_root)
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
	##
	## When [param ack] is [code]true[/code], the remote peer responds with
	## [signal push_acknowledged] after processing the push. The caller should
	## await that signal before taking actions that depend on the remote state
	## being up-to-date.
func push_to(peer_id: int, ack: bool = false) -> void:
	pull_from_scene()
	_request_push.rpc_id(peer_id, bound_entity.serialize(), ack)


# RPC called by a client to push its serialized entity state to this peer.
@rpc("any_peer", "call_local", "reliable")
func _request_push(bytes: PackedByteArray, ack: bool = false) -> void:
	bound_entity.deserialize(bytes)
	push_to_scene()
	_on_state_changed()
	if ack:
		var sender_id := multiplayer.get_remote_sender_id()
		if sender_id == multiplayer.get_unique_id():
			push_acknowledged.emit()
		else:
			var tp: TPComponent = owner.get_node_or_null("%TPComponent")
			if tp:
				tp._rpc_push_ack.rpc_id(sender_id)


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

	var spawner := SpawnerComponent.unwrap(root)
	if spawner and not spawner.entity_id.is_empty():
		return spawner.entity_id
	return StringName(root.name)


# Sibling hook: SpawnerComponent dispatches this in the unique window before
# any other component's _enter_tree, with authority + identity already settled.
# Server-side hydrate happens here so TPComponent can read current_scene_path
# from a hydrated bound_entity during its own _enter_tree.
func _on_entity_spawning(_spawner: SpawnerComponent) -> void:
	hydrate_from_db()


## Flushes the current entity state to [member database] immediately.
func flush() -> Error:
	return _flush()


# Internal flush implementation.
func _flush() -> Error:
	if not database or table_name.is_empty():
		_dbg.warn("Cannot flush; database or table_name is missing.",
				func(m): push_warning(m))
		return ERR_UNCONFIGURED
	var entity_id := _get_entity_id()
	var db_err := database.transaction(func(tx: NetwDatabase.TransactionContext) -> void:
		tx.queue_upsert(table_name, entity_id, bound_entity.to_dict())
	)
	if db_err == OK:
		_dbg.trace("State flushed (table=%s, id=%s).", [table_name, entity_id])
	else:
		_dbg.warn("Database upsert failed. Error: %s", [error_string(db_err)],
				func(m): push_warning(m))
	return db_err


## Hydrates this component from [member database], fetching the record
## for the entity ID returned by [method _get_entity_id].
##
## Applies the result to [member bound_entity] and pushes it to the scene.
## No-op when [member database] or [member table_name] is missing.
func hydrate_from_db() -> void:
	if not database or table_name.is_empty():
		return
	var entity := database.table(table_name).fetch(_get_entity_id())
	hydrate(entity.to_dict() if entity else {})


## Hydrates this component from [param record].
##
## Applies the dictionary to [member bound_entity] and pushes it to the scene.
## When [param record] is empty, seeds from scene defaults instead.
func hydrate(record: Dictionary) -> void:
	if not _initialized:
		_instantiate_sync()
	_seed_entity_from_scene()
	
	if not record.is_empty():
		bound_entity.from_dict(record)
		push_to_scene()
		_dbg.info("State hydrated (table=%s, id=%s).",
				[table_name, _get_entity_id()])
	else:
		_dbg.debug("No record found (table=%s, id=%s); using scene defaults.",
				[table_name, _get_entity_id()])
	
	loaded.emit()


# Deserializes a network byte array into the entity and pushes the result to the scene.
func _deserialize_scene(bytes: PackedByteArray) -> void:
	_dbg.trace("deserializing scene (%d bytes).", [bytes.size()])
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
	_flush()
	state_changed.emit()
	client_synchronized.emit()


# Initializes the synchronizer and registers the entity schema with [member database].
func _instantiate_sync() -> void:
	if _save_span:
		_save_span.step("instantiate_begin", {
			in_tree = is_inside_tree(),
			tracked_count = _properties.size(),
		})
	
	_setup_sync()
	_seed_entity_from_scene()
	assert(_initialized)
	
	if database:
		database.bind(self, _save_span)
	
	instantiated.emit()


func _notification(what: int) -> void:
	if not Engine.is_editor_hint() and what == NOTIFICATION_WM_CLOSE_REQUEST:
		var bucket := _get_bucket()
		if bucket and not bucket.shutting_down:
			bucket.shutting_down = true
			_handle_shutdown()


# Returns editor warnings when the configuration is incomplete.
func _get_configuration_warnings() -> PackedStringArray:
	var warnings: PackedStringArray = []
	
	if root_path != NodePath("."):
		warnings.append(
			"SaveComponent requires [code]root_path[/code] to be set to "
			+ "[code].[/code] (self) for proper sync resolution."
		)
	
	if not database:
		warnings.append("'database' must be assigned for persistence.")
	else:
		var path := database.resource_path
		if path.is_empty() or path.contains("::"):
			warnings.append(
				"'database' is an embedded resource. Save it as a .tres file "
				+ "to avoid resource-path conflicts and data loss."
			)
		
		if table_name.is_empty():
			warnings.append("'table_name' must be set when 'database' is assigned.")
		elif database.has_method("has_table") and not database.has_table(table_name):
			warnings.append(
				"Table [code]%s[/code] is not yet registered. " % [table_name] + \
				"Declare it on the database resource or bind a [SaveComponent]."
			)
	
	var config := replication_config
	if not config or config.get_properties().is_empty():
		warnings.append("No properties are tracked. Pick properties in the Replication panel.")
	else:
		var validation_root := self
		for prop in config.get_properties():
			var res := validation_root.get_node_and_resource(prop)
			if not res[0] or res[2].is_empty():
				warnings.append(
					"Property [code]%s[/code] not found on SaveComponent. " % [str(prop)]
					+ "Paths are resolved relative to SaveComponent; use [code]..:position[/code] "
					+ "to reference the owner node."
				)
	
	return warnings


# ── Static helpers ─────────────────────────────────────────────────────────────

# Internal entry point: saves all components registered in [param ctx].
static func _save_all_in(ctx: NetwPeerContext) -> void:
	if not ctx:
		return
	var bucket := ctx.get_bucket(Bucket) as Bucket
	if not bucket:
		return
	for component in bucket.registered:
		if not component.is_multiplayer_authority():
			continue
		if component.multiplayer.is_server():
			component.pull_from_scene()
			component._flush()
		else:
			component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)


# ── Session / bucket access ────────────────────────────────────────────────────

## Returns the [NetwPeerContext] for the local peer.
func get_peer_context() -> NetwPeerContext:
	if not is_inside_tree() or not multiplayer:
		return null
	var mt := MultiplayerTree.resolve(self)
	return mt.get_peer_context(multiplayer.get_unique_id()) if mt else null


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
	SaveComponent._save_all_in(get_peer_context())
	_dbg.info("All states saved. Quitting.")
	(Engine.get_main_loop() as SceneTree).quit()
