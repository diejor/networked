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
class_name NetwPersistenceEngine
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


## Advances per-column accumulators and returns the write the columns whose
## interval elapsed this step are owed, or an empty [Dictionary] when nothing
## changed.
##
## The write is returned rather than issued so the snapshot loop can group every
## engine due this tick into one transaction per database. One transaction per
## engine per tick is the same bytes and N times the commits.
## [codeblock]
## Dictionary
## ┠╴db      (NetwDatabase)
## ┠╴table   (StringName)
## ┠╴id      (StringName)
## ┖╴values  (Dictionary)  column -> value
## [/codeblock]
## [br][br][b]Server Only.[/b]
func snapshot_tick(delta: float) -> Dictionary:
	var due: Array[StringName] = []
	for col in _columns:
		col.accum += delta
		var interval := col.interval if col.interval > 0.0 \
		else _config.default_interval
		if col.accum >= interval:
			col.accum = 0.0
			due.append(col.property)
	if due.is_empty():
		return { }
	var changed: Array[StringName] = []
	var current := gather(due)
	for key in due:
		if _last_flushed.get(key) != current.get(key):
			changed.append(key)
	if changed.is_empty():
		return { }
	var db := database()
	var table := _table()
	if not db or table.is_empty():
		return { }
	_ensure_schema()
	var values: Dictionary = { }
	for key in changed:
		values[key] = current[key]
	return {
		&"db": db,
		&"table": table,
		&"id": _record_id(),
		&"values": values,
	}


## Adopts [param values] as the last flushed state after the snapshot loop
## committed them, and emits [signal flushed].
func commit_snapshot(values: Dictionary) -> void:
	for key: StringName in values:
		_last_flushed[key] = values[key]
	flushed.emit()


# Registers the persisted columns as the table schema so the backend knows them
# before the first query. Idempotent, run lazily on the first flush or hydrate
# so an instance database override set after construction still takes effect.
#
# The declaration carries the column types, reflected through the node that
# owns each property, which is what lets the database reject a stored value of
# the wrong shape instead of assigning it.
func _ensure_schema() -> void:
	if _schema_registered:
		return
	var db := database()
	var table := _table()
	if not db or table.is_empty():
		return
	var declaration := NetwSchemaModel.Declaration.new(table)
	declaration.replicated = false
	for col in _columns:
		var node := col.node_ref.get_ref() as Node
		var script := node.get_script() as Script if is_instance_valid(node) \
		else null
		declaration.column(
			col.property,
			NetwPropertySet.column_type_for(script, node, col.property),
			1,
			null,
		)
	db.declare_table(table, declaration)
	_schema_registered = true
	_claim_record_id(db, table)

# One live record id per (database, table) pair. A second entity claiming a
# claimed triple would silently overwrite the first one's saves every tick, and
# the two would fight for the same row for the rest of the session.
static var _claims: Dictionary[String, int] = { }


# Warns when this entity's record id collides with another live entity's, and
# when the id is the node name, which a scene tree renames freely.
func _claim_record_id(db: NetwDatabase, table: StringName) -> void:
	var id := _record_id()
	if id.is_empty():
		return
	var owner := _owner()
	if not is_instance_valid(owner):
		return
	if _config.record_id_provider.is_empty() \
			and (not (_entity_ref.get_ref() as NetwEntity) \
							or (_entity_ref.get_ref() as NetwEntity).entity_id.is_empty()):
		Netw.dbg.warn(
			"Persisted entity '%s' has no record id provider and no entity id, "
			+ "so it saves under its node name. A scene that renames the node, "
			+ "or a second instance of it, loads the wrong row. Declare "
			+ "record_id_provider or set NetwEntity.entity_id.",
			[owner.name],
			func(m): push_warning(m),
		)
	var key := "%d/%s/%s" % [db.get_instance_id(), table, id]
	var held := int(_claims.get(key, 0))
	if held != 0 and held != owner.get_instance_id() \
			and is_instance_valid(instance_from_id(held)):
		Netw.dbg.warn(
			"Persisted entity '%s' claims record '%s.%s', which another live "
			+ "entity already holds. Both flush the same row every tick and "
			+ "the last write wins.",
			[owner.name, table, id],
			func(m): push_warning(m),
		)
		return
	_claims[key] = owner.get_instance_id()


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
			col.property,
		) as NetwScriptModel.PropertyConfig
		if not cfg or cfg.write_policy == NetwScriptModel.Policy.AUTHORITY:
			continue
		# L2: a client-owned field that opted into no lane never reaches the
		# server, so a flush can only save stale server-side data.
		var has_lane := cfg.in_state_set or cfg.in_input_set \
				or cfg.lane == NetwPropertySet.Lane.RETAINED
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
