## Server-sided persistence surface for one [MultiplayerTree], owned by
## [NetwMultiplayer].
##
## [member NetwMultiplayer.persistence] is never [code]null[/code]. The interface
## hosts one [NetwPersistenceInterface.PersistenceEngine] per spawned entity whose archetype declared
## [method Netw.configure_persistence], runs a single server-side snapshot loop
## over every engine, and owns the graceful-shutdown drain. The database only ever
## sees what the server sees, so persistence adds no wire format: every flush reads
## server-side live values and every hydrate applies on the server and propagates
## through the ordinary spawn and sync machinery. On clients the interface exists
## but registers nothing, because every trigger is server-gated.
## [codeblock]
## NetwPersistenceInterface (server)
##  ┠╴ engine per entity   frozen persisted-column set, per-column accumulators
##  ┠╴ snapshot loop        one tick advances every engine, flushes the due subset
##  ┖╴ shutdown drain       broadcast, flush all engines, drain backends, quit
## [/codeblock]
## The entity facade hands one engine out through [member NetwEntity.persistence].
class_name NetwPersistenceInterface
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef

# One engine per persistence-declaring entity, keyed by the RefCounted NetwEntity.
var _engines: Dictionary[NetwEntity, PersistenceEngine] = { }

# Set once the close notification begins the drain, so a second WM_CLOSE is inert.
var _shutting_down: bool = false

## Seconds the server waits after broadcasting the shutdown notice before saving
## and quitting, giving clients time to react before the connection drops.
var shutdown_notify_delay: float = 0.5


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# Server-or-offline test. An offline rig with no peer counts as server so unit
# suites exercise the server-only snapshot loop, mirroring the addon's other
# server guards.
func _is_server() -> bool:
	var api := _api()
	if not api:
		return true
	if not api.has_multiplayer_peer():
		return true
	return api.is_server()


## Returns the [NetwPersistenceInterface.PersistenceEngine] for [param entity], building it the first time
## a persistence-declaring entity is seen, or [code]null[/code] when its archetype
## declares no persistence. The engine is registered into the snapshot loop and a
## final flush is scheduled for when the owner leaves the tree.
func engine_for(entity: NetwEntity) -> PersistenceEngine:
	if not entity or not is_instance_valid(entity.owner):
		return null
	var existing := _engines.get(entity)
	if existing:
		return existing
	var config := NetwScriptModel.get_persistence_config(entity.owner)
	# A scriptless instance declares persistence through metadata instead of a
	# script config, the pack-surviving path the test builders use.
	if not config and (
			entity.owner.has_meta(PersistenceEngine.META_DATABASE)
			or entity.owner.has_meta(PersistenceEngine.META_COLUMNS)):
		config = NetwScriptModel.PersistenceConfig.new()
	if not config:
		return null
	var engine := PersistenceEngine.new(entity, config)
	if engine.columns_empty():
		Netw.dbg.warn(
			"configure_persistence on '%s' declares no persisted field. "
			+ "Mark a field with configure_property(...).persisted().",
			[entity.owner.name],
			func(m): push_warning(m),
		)
	_engines[entity] = engine
	_lint_engine(engine)
	_arm_shutdown_guard()
	if not entity.owner.tree_exiting.is_connected(_on_owner_exiting):
		entity.owner.tree_exiting.connect(
			_on_owner_exiting.bind(entity), CONNECT_ONE_SHOT,
		)
	return engine


# Blocks the auto-quit on a host with persisted entities so the window-close
# notification can run the graceful drain before the process exits.
func _arm_shutdown_guard() -> void:
	if not _is_server():
		return
	var api := _api()
	var scene_tree := Engine.get_main_loop() as SceneTree
	if api and api.is_host and scene_tree:
		scene_tree.set_auto_accept_quit(false)


# Final flush and deregister when a persisted entity leaves the tree, so a despawn
# never drops the last snapshot window.
func _on_owner_exiting(entity: NetwEntity) -> void:
	var engine := _engines.get(entity)
	if engine and _is_server():
		engine.flush()
	_engines.erase(entity)


## Flushes every registered engine immediately. Server only in effect. Used before
## a graceful disconnect so no snapshot window is dropped.
func flush_all() -> void:
	if not _is_server():
		return
	for entity in _engines.keys():
		var engine := _engines[entity]
		if is_instance_valid(entity.owner):
			engine.flush()


## Advances every engine's per-column accumulators by [param delta] and flushes the
## due subset. Server only in effect. Driven by [method MultiplayerTree._process].
func tick(delta: float) -> void:
	if not _is_server():
		return
	for entity in _engines.keys():
		var engine := _engines[entity]
		if is_instance_valid(entity.owner):
			engine.snapshot_tick(delta)
		else:
			_engines.erase(entity)


## Broadcasts the shutdown notice, flushes every engine, drains write-behind
## backends, then quits. Idempotent. Driven by the owner tree's window-close
## notification.
## [br][br][b]Server Only.[/b]
func handle_shutdown() -> void:
	if _shutting_down or not _is_server():
		return
	_shutting_down = true
	var api := _api()
	var scene_tree := Engine.get_main_loop() as SceneTree
	if api and api.is_host:
		api.notify_shutdown("Server is shutting down.")
		if scene_tree:
			await scene_tree.create_timer(shutdown_notify_delay).timeout
	var drained: Dictionary[NetwDatabase, bool] = { }
	for entity in _engines.keys():
		var engine := _engines[entity]
		if is_instance_valid(entity.owner):
			@warning_ignore("redundant_await")
			await engine.flush()
			if engine.database():
				drained[engine.database()] = true
	for db in drained:
		await _drain_database(db)
	if scene_tree:
		scene_tree.quit()


# Drains a write-behind backend (e.g. NakamaDatabase) so its last flush window is
# not lost on quit. Synchronous backends have no drain and this is a no-op.
func _drain_database(db: NetwDatabase) -> void:
	if not db or not db.backend:
		return
	if db.backend.has_method("drain"):
		@warning_ignore("redundant_await")
		await db.backend.drain()


# Warns at schema freeze when a persisted client-owned field is either double
# authority (L1) or has no delivery that reaches the server (L2).
func _lint_engine(engine: PersistenceEngine) -> void:
	engine._lint(_api())


## One entity's persistence state: a frozen persisted-column set over the live
## scene, plus the flush and hydrate operations against the archetype's database.
##
## The scene IS the record. There is no shadow store to keep coherent: you assign
## a property in plain GDScript and [method gather] reads it at flush time. A
## column reads and writes the live property on the node that declared it, so a
## sibling component's persisted field lands in the same row as the root's.
## [codeblock]
## # server, in the orphan window Netw.replicate leaves open:
## var player := PlayerScene.instantiate()
## var entity := Netw.replicate(player, participant)
## await entity.persistence.hydrate()   # the saved row lands on live properties
## scene.add_child(player)
##
## player.gold += 100                   # a plain assignment IS the record;
##                                      # the snapshot loop's next flush()
##                                      # upserts the changed column
## [/codeblock]
class PersistenceEngine:
	extends RefCounted

	## Emitted after a row is fetched and applied, even when no row existed.
	signal hydrated
	## Emitted after a flush upserts the current values.
	signal flushed

	## Instance override for the archetype database, read from
	## [code]owner.get_meta(&"netw_persistence_database")[/code] when present.
	const META_DATABASE := &"netw_persistence_database"
	## Instance override for the archetype table, read from
	## [code]owner.get_meta(&"netw_persistence_table")[/code] when present.
	const META_TABLE := &"netw_persistence_table"
	## Extra persistence columns declared on a scriptless instance, an
	## [Array] of [code]{property, interval}[/code] read from the entity root. The
	## pack-surviving declaration the test builders use in place of a script.
	const META_COLUMNS := &"netw_persistence_columns"

	var _entity_ref: WeakRef
	var _config: NetwScriptModel.PersistenceConfig
	var _columns: Array[_Column] = []
	# Last flushed values, so the snapshot loop only writes a changed subset.
	var _last_flushed: Dictionary = { }
	var _schema_registered: bool = false


	func _init(entity: NetwEntity, config: NetwScriptModel.PersistenceConfig) -> void:
		_entity_ref = weakref(entity)
		_config = config
		_build_columns(entity)


	# Freezes the persisted-column set by scanning the entity subtree once. Each
	# node inside this entity that declares a persisted property contributes a
	# column reading the live value on that node.
	func _build_columns(entity: NetwEntity) -> void:
		var root := entity.owner
		if not is_instance_valid(root):
			return
		var seen: Dictionary[StringName, bool] = { }
		for node in _entity_nodes(entity, root):
			var configs := NetwScriptModel.get_node_property_configs(node)
			for property: StringName in configs:
				var cfg := configs[property] as NetwScriptModel.PropertyConfig
				if not cfg or not cfg.is_persisted:
					continue
				if seen.has(property):
					_warn_duplicate(property, root)
				seen[property] = true
				var col := _Column.new()
				col.node_ref = weakref(node)
				col.property = property
				col.interval = cfg.persist_interval
				_columns.append(col)
		# Metadata-declared columns (the scriptless builder path) read from the
		# root and join the same row.
		for entry in root.get_meta(META_COLUMNS, [] as Array):
			var property: StringName = entry.get("property", &"")
			if property.is_empty() or seen.has(property):
				continue
			seen[property] = true
			var col := _Column.new()
			col.node_ref = weakref(root)
			col.property = property
			col.interval = float(entry.get("interval", 0.0))
			_columns.append(col)


	func _warn_duplicate(property: StringName, root: Node) -> void:
		Netw.dbg.warn(
			"Persisted column '%s' declared on more than one node under entity "
			+ "'%s'; the last write wins.",
			[property, root.name],
			func(m): push_warning(m),
		)


	# Nodes belonging to this entity: the root and every descendant that resolves
	# back to this entity, stopping at nested entity roots.
	func _entity_nodes(entity: NetwEntity, root: Node) -> Array[Node]:
		var out: Array[Node] = [root]
		var stack: Array[Node] = root.get_children()
		while not stack.is_empty():
			var node: Node = stack.pop_back()
			if NetwEntity.of(node) != entity:
				continue
			out.append(node)
			stack.append_array(node.get_children())
		return out


	## Returns [code]true[/code] when the archetype declared no persisted field.
	func columns_empty() -> bool:
		return _columns.is_empty()


	## Returns the effective [NetwDatabase], the instance override when present else
	## the archetype's declaration, or [code]null[/code].
	func database() -> NetwDatabase:
		var owner := _owner()
		if is_instance_valid(owner) and owner.has_meta(META_DATABASE):
			return owner.get_meta(META_DATABASE) as NetwDatabase
		return _config.db


	# The effective table, the instance override when present else the archetype's
	# declaration.
	func _table() -> StringName:
		var owner := _owner()
		if is_instance_valid(owner) and owner.has_meta(META_TABLE):
			return owner.get_meta(META_TABLE)
		return _config.table_name


	func _owner() -> Node:
		var entity := _entity_ref.get_ref() as NetwEntity
		return entity.owner if entity else null


	## [code]true[/code] when the archetype wants the server to hydrate before the
	## spawn frame is snapshotted.
	func wants_spawn_hydration() -> bool:
		return _config.hydrate_on_spawn_enabled


	## Reads the persisted columns from the live scene, or the subset in
	## [param keys] when non-empty.
	func gather(keys: Array = []) -> Dictionary:
		var out: Dictionary = { }
		for col in _columns:
			if not keys.is_empty() and col.property not in keys:
				continue
			var node := col.node_ref.get_ref() as Node
			if is_instance_valid(node):
				out[col.property] = node.get(col.property)
		return out


	## Writes fetched values onto the live scene properties.
	func apply(data: Dictionary) -> void:
		for col in _columns:
			if not data.has(col.property):
				continue
			var node := col.node_ref.get_ref() as Node
			if is_instance_valid(node):
				node.set(col.property, data[col.property])


	## [code]true[/code] when the live values differ from the last flush.
	func is_dirty() -> bool:
		return gather() != _last_flushed


	## Fetches the saved row by record id, applies it, and emits [signal hydrated].
	## Emits even on a missing row, so a first-play entity keeps its scene defaults.
	## [br][br][b]Server Only.[/b]
	func hydrate() -> Error:
		var db := database()
		var table := _table()
		if not db or table.is_empty():
			return ERR_UNCONFIGURED
		_ensure_schema()
		var rid := _record_id()
		@warning_ignore("redundant_await")
		var record := await db.table(table).fetch(rid)
		var data := record.to_dict() if record else { }
		if not data.is_empty():
			apply(data)
			_last_flushed = gather()
		hydrated.emit()
		return OK


	## Gathers the persisted columns (or the subset in [param keys]) and writes them
	## as one merged upsert, then emits [signal flushed].
	## [br][br][b]Server Only.[/b]
	func flush(keys: Array = []) -> Error:
		var db := database()
		var table := _table()
		if not db or table.is_empty():
			return ERR_UNCONFIGURED
		_ensure_schema()
		var subset := gather(keys)
		if subset.is_empty():
			return OK
		var rid := _record_id()
		@warning_ignore("redundant_await")
		var err := await db.transaction(
			func(tx: NetwDatabase.TransactionContext) -> void:
				tx.queue_upsert(table, rid, subset)
		)
		if err == OK:
			for key: StringName in subset:
				_last_flushed[key] = subset[key]
			flushed.emit()
		return err


	# Advances per-column accumulators and flushes the columns whose interval
	# elapsed this step, but only when a value actually changed.
	func snapshot_tick(delta: float) -> void:
		var due: Array[StringName] = []
		for col in _columns:
			col.accum += delta
			var interval := col.interval if col.interval > 0.0 \
					else _config.default_interval
			if col.accum >= interval:
				col.accum = 0.0
				due.append(col.property)
		if due.is_empty():
			return
		var changed: Array[StringName] = []
		var current := gather(due)
		for key in due:
			if _last_flushed.get(key) != current.get(key):
				changed.append(key)
		if not changed.is_empty():
			flush(changed)


	# Registers the persisted columns as the table schema so the backend knows them
	# before the first query. Idempotent, run lazily on the first flush or hydrate
	# so an instance database override set after construction still takes effect.
	func _ensure_schema() -> void:
		if _schema_registered:
			return
		var db := database()
		var table := _table()
		if not db or table.is_empty():
			return
		var columns: Array[StringName] = []
		for col in _columns:
			if col.property not in columns:
				columns.append(col.property)
		db.declare_table(table, columns)
		_schema_registered = true


	# The stable record id: the archetype's provider method, else the entity id,
	# else the node name.
	func _record_id() -> StringName:
		var root := _entity_ref.get_ref()
		var entity := root as NetwEntity
		var owner: Node = entity.owner if entity else null
		if not is_instance_valid(owner):
			return &""
		if not _config.record_id_provider.is_empty() \
				and owner.has_method(_config.record_id_provider):
			return StringName(owner.call(_config.record_id_provider))
		if entity and not entity.entity_id.is_empty():
			return entity.entity_id
		return StringName(owner.name)


	# Schema-freeze lints. A persisted client-owned field that another declaration
	# also authority-writes is double authority (L1); one whose delivery never
	# reaches the server can never be flushed with the client's value (L2).
	func _lint(_api: NetwMultiplayer) -> void:
		var entity := _entity_ref.get_ref() as NetwEntity
		if not entity or not is_instance_valid(entity.owner):
			return
		for col in _columns:
			var node := col.node_ref.get_ref() as Node
			if not is_instance_valid(node):
				continue
			var cfg := NetwScriptModel.get_node_property_configs(node).get(
					col.property) as NetwScriptModel.PropertyConfig
			if not cfg or cfg.write_policy == NetwScriptModel.Policy.AUTHORITY:
				continue
			# L2: a client-owned field that opted into no lane never reaches the
			# server, so a flush can only save stale server-side data.
			var has_lane := cfg.in_state_set or cfg.in_input_set \
					or cfg.lane == NetwSyncSet.Lane.RETAINED
			if not has_lane:
				Netw.dbg.warn(
					"Persisted field '%s' is client-owned but rides no lane; the "
					+ "server never sees the client's value, so it saves stale "
					+ "data. Add a sync axis (state/input/retained) or make it "
					+ "authority-written.",
					[col.property],
					func(m): push_warning(m),
				)
				continue
			# L1: also governed by another synchronizer is double authority.
			var real_path := entity.property_path(node, col.property)
			if not real_path.is_empty() and entity.governs_property(real_path):
				Netw.dbg.warn(
					"Persisted client-owned field '%s' is also governed by a "
					+ "synchronizer (double authority); did you mean an "
					+ "authority write policy?",
					[col.property],
					func(m): push_warning(m),
				)


	class _Column extends RefCounted:
		var node_ref: WeakRef
		var property: StringName
		var interval: float = 0.0
		var accum: float = 0.0
