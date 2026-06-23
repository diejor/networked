## [NetwDatabaseBackend] that persists records to Nakama storage, host-only.
##
## Nakama storage has no partial-field update and every write is a network round
## trip, so this backend never blocks a gameplay write. [method upsert],
## [method commit], and [method delete] mutate an in-memory mirror of the open
## slot and return [constant OK] at once, while a debounced loop flushes the
## dirty records every [member flush_interval] seconds. A read serves the mirror
## first and falls back to a remote fetch only on a miss.
## [codeblock]
## upsert/commit/delete ─╴ mutate mirror, mark dirty ─╴ return OK (no await)
##                                  │
##                    flush_interval│ debounced
##                                  ▼
##         await write/delete storage objects through NakamaWrapper
## [/codeblock]
##
## The backend needs an authenticated [NakamaSessionService], which it resolves
## lazily through [method MultiplayerTree.get_nakama_session] and
## [NakamaLobbyDirectory] sets up during connection. Records cross Nakama's
## JSON-only storage as a [JSON] envelope wrapping a [Marshalls] base64 of the
## record, so a [Vector2] or a [Color] survives the round trip. Queries filter
## the mirror locally instead of pushing the predicate to the server, the same
## as [FileSystemDatabase].
class_name NakamaDatabase
extends NetwDatabaseBackend

# Collection that holds the per-app slot index, since Nakama cannot list
# collections. Keyed under app_id so several apps on one server stay separate.
const _SLOT_INDEX_SUFFIX := "__slots__"
const _SLOT_INDEX_KEY := "index"

## Application scope folded into every collection name ahead of the save slot.
@export var app_id: String = "networked"

## Seconds between debounced flushes of dirty records to Nakama. A shutdown
## forces an immediate flush through [method drain] regardless of this cadence.
@export_range(0.5, 30.0, 0.5, "suffix:s") var flush_interval: float = 5.0

# Shared authentication and the storage-only wrapper bound to it.
var _session: NakamaSessionService
var _wrapper: NakamaWrapper

# Declared schema and the open save slot.
var _schema: Dictionary = { }
var _slot: String = "default"

# table -> { id -> Dictionary }: the full mirror of the open slot.
var _cache: Dictionary = { }
# table -> { id -> true }: records mutated since the last successful flush.
var _dirty: Dictionary = { }
# table -> { id -> true }: records deleted but not yet removed from Nakama.
var _deletes: Dictionary = { }

var _ready: bool = false
var _flushing: bool = false
var _running: bool = false

# ── NetwDatabaseBackend overrides ────────────────────────────────────────


## Overrides [method NetwDatabaseBackend.initialize] to declare the schema,
## open the specified save [param slot], register it in the slot index, and
## start the debounced flush loop.
func initialize(schema: Dictionary, slot: String = "") -> Error:
	_schema = schema
	_slot = slot if not slot.is_empty() else "default"
	_cache.clear()
	_dirty.clear()
	_deletes.clear()
	_ready = false

	# Record the slot in the index so list_namespaces can find it later.
	if await _ensure_session():
		await _register_slot(_slot)

	_start_flush_loop()
	return OK


## Overrides [method NetwDatabaseBackend.upsert] to write or update a record
## in the local cache and mark it dirty for the next debounced flush.
func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
	var bucket := _cache_table(table)
	if not bucket.has(id):
		bucket[id] = { }
	for key in data:
		bucket[id][key] = data[key]
	_mark(_dirty, table, id)
	if _deletes.has(table):
		_deletes[table].erase(id)
	return OK


## Overrides [method NetwDatabaseBackend.find_by_id] to read a record from the
## local cache, falling back to a remote read from Nakama storage if it is not
## cached.
func find_by_id(table: StringName, id: StringName) -> Dictionary:
	if _cache.has(table) and _cache[table].has(id):
		return _cache[table][id].duplicate()

	# Cold: lazy fetch-on-miss, the unconditional fallback under any warm policy.
	if not await _ensure_session():
		return { }
	var rows := await _wrapper.read_storage_objects(
		[{ collection = _collection(table), key = String(id) }],
	)
	if rows.is_empty():
		return { }
	var record := _decode(rows[0].get("value"))
	if record.is_empty():
		return { }
	_cache_table(table)[id] = record
	return record.duplicate()


## Overrides [method NetwDatabaseBackend.find_all] to return all cached
## records matching [param filter].
func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
	var results: Array[Dictionary] = []
	if not _cache.has(table):
		return results
	for id in _cache[table]:
		var record: Dictionary = _cache[table][id]
		if _matches_filter(record, filter):
			results.append(record.duplicate())
	return results


## Overrides [method NetwDatabaseBackend.delete] to remove a record from the
## local cache and queue its deletion on Nakama during the next flush.
func delete(table: StringName, id: StringName) -> Error:
	if _cache.has(table):
		_cache[table].erase(id)
	if _dirty.has(table):
		_dirty[table].erase(id)
	_mark(_deletes, table, id)
	return OK


## Overrides [method NetwDatabaseBackend.warm] to pre-load database records
## into the local cache according to the provided [param directives].
func warm(directives: Array) -> Error:
	if not await _ensure_session():
		return ERR_CANT_CONNECT
	for directive in directives:
		var table: StringName = directive.table
		var request: WarmRequest = directive.request
		match request.kind:
			WarmRequest.Kind.ALL, WarmRequest.Kind.FILTER:
				# Opaque blobs cannot be server-filtered, so warm the whole table
				# and let find_all filter the cache locally.
				await _warm_table(table)
			WarmRequest.Kind.IDS:
				await _warm_ids(table, request.id_list)
			_:
				pass
	_ready = true
	return OK


## Overrides [method NetwDatabaseBackend.list_namespaces] to list all
## registered save-slot names stored in the Nakama slot index.
func list_namespaces() -> Array[StringName]:
	var out: Array[StringName] = []
	if not await _ensure_session():
		return out
	for slot in await _read_slot_index():
		out.append(StringName(slot))
	return out


## Overrides [method NetwDatabaseBackend.delete_namespace] to delete all
## storage objects under [param slot] and remove it from the Nakama slot index.
func delete_namespace(slot: String) -> Error:
	if slot.is_empty():
		return ERR_INVALID_PARAMETER
	if not await _ensure_session():
		return ERR_CANT_CONNECT

	# Remove every known-table record under the slot, then drop it from the index.
	var ids: Array = []
	for table: StringName in _schema:
		var listing := await _wrapper.list_storage_objects(
			"%s.%s.%s" % [app_id, slot, table],
		)
		for object in listing.get("objects", []):
			ids.append({ collection = "%s.%s.%s" % [app_id, slot, table], key = object.key })
	if not ids.is_empty():
		await _wrapper.delete_storage_objects(ids)

	var slots := await _read_slot_index()
	slots.erase(slot)
	await _write_slot_index(slots)
	return OK

# ── Drain ─────────────────────────────────────────────────────────────────────


## Flushes the queue and waits for acknowledgment, bounded by [param timeout_s].
##
## A write returns before it reaches Nakama, so a quit or a player leave would
## otherwise drop the last [member flush_interval] window. The [SaveComponent]
## shutdown path calls this so that final batch lands. Returns [constant OK] when
## the queue drained or [constant ERR_TIMEOUT] when it did not within the bound.
func drain(timeout_s: float = 5.0) -> Error:
	var loop := Engine.get_main_loop() as SceneTree
	var deadline := Time.get_ticks_msec() + int(timeout_s * 1000.0)
	while not _queue_empty() and Time.get_ticks_msec() < deadline:
		if _flushing:
			if loop:
				await loop.create_timer(0.05).timeout
			else:
				break
		else:
			await _flush()
	return OK if _queue_empty() else ERR_TIMEOUT

## Stops the debounced flush loop. Call after a final [method drain] when
## retiring the backend so the timer loop does not outlive it.
func stop() -> void:
	_running = false

# ── Flush ─────────────────────────────────────────────────────────────────────


# Drives debounced flushes for the life of the backend. One pass per interval.
func _start_flush_loop() -> void:
	if _running:
		return
	_running = true
	_flush_loop()


func _flush_loop() -> void:
	var loop := Engine.get_main_loop() as SceneTree
	while _running and loop:
		await loop.create_timer(flush_interval).timeout
		await _flush()


# Builds the batch synchronously, moves dirty/deletes out before awaiting, then
# writes. Concurrent upserts during the await refill an empty set and are caught
# next pass, so no write is lost. Failed batches merge back for retry.
func _flush() -> void:
	if _flushing:
		return
	if _queue_empty():
		return
	if not await _ensure_session():
		return

	var writes: Array = []
	var pending_writes: Dictionary = { }
	for table in _dirty:
		for id in _dirty[table]:
			if not (_cache.has(table) and _cache[table].has(id)):
				continue
			writes.append({
				collection = _collection(table),
				key = String(id),
				value = _encode(_cache[table][id]),
			})
			_mark(pending_writes, table, id)

	var removals: Array = []
	var pending_deletes: Dictionary = { }
	for table in _deletes:
		for id in _deletes[table]:
			removals.append({ collection = _collection(table), key = String(id) })
			_mark(pending_deletes, table, id)

	# Move-out: clear the queues before the await window opens.
	_dirty.clear()
	_deletes.clear()

	_flushing = true
	var wrote := true
	var removed := true
	if not writes.is_empty():
		wrote = await _wrapper.write_storage_objects(writes)
	if not removals.is_empty():
		removed = await _wrapper.delete_storage_objects(removals)
	_flushing = false

	if not wrote:
		_merge(_dirty, pending_writes)
	if not removed:
		_merge(_deletes, pending_deletes)

# ── Warming ───────────────────────────────────────────────────────────────────


func _warm_table(table: StringName) -> void:
	var cursor := ""
	while true:
		var listing := await _wrapper.list_storage_objects(_collection(table), 100, cursor)
		for object in listing.get("objects", []):
			var record := _decode(object.get("value"))
			if not record.is_empty():
				_cache_table(table)[StringName(object.key)] = record
		cursor = String(listing.get("cursor", ""))
		if cursor.is_empty():
			break


func _warm_ids(table: StringName, ids: Array) -> void:
	if ids.is_empty():
		return
	var query: Array = []
	for id in ids:
		query.append({ collection = _collection(table), key = String(id) })
	for row in await _wrapper.read_storage_objects(query):
		var record := _decode(row.get("value"))
		if not record.is_empty():
			_cache_table(table)[StringName(row.get("key"))] = record

# ── Slot index ────────────────────────────────────────────────────────────────


func _register_slot(slot: String) -> void:
	var slots := await _read_slot_index()
	if slot in slots:
		return
	slots.append(slot)
	await _write_slot_index(slots)


func _read_slot_index() -> Array:
	var rows := await _wrapper.read_storage_objects(
		[{ collection = _slot_index_collection(), key = _SLOT_INDEX_KEY }],
	)
	if rows.is_empty():
		return []
	var value = rows[0].get("value")
	if typeof(value) == TYPE_DICTIONARY and value.has("slots"):
		var out: Array = []
		for slot in value["slots"]:
			out.append(String(slot))
		return out
	return []


func _write_slot_index(slots: Array) -> void:
	await _wrapper.write_storage_objects([{
		collection = _slot_index_collection(),
		key = _SLOT_INDEX_KEY,
		value = JSON.stringify({ "slots": slots }),
	}])

# ── Session + helpers ─────────────────────────────────────────────────────────


# Resolves the shared session lazily (the tree exists by the first network op)
# and ensures it is authenticated. Returns false when Nakama is unavailable.
func _ensure_session() -> bool:
	if _session == null:
		_session = _find_session()
		if _session == null:
			return false
		_wrapper = NakamaWrapper.new()
		_wrapper.use_session(_session)
	if _session.is_authenticated():
		return true
	var res := await _session.connect_async()
	return bool(res.get("ok", false))


# Walks the live scene tree for the session-global NakamaSessionService.
func _find_session() -> NakamaSessionService:
	var loop := Engine.get_main_loop() as SceneTree
	if loop == null:
		return null
	var mt := _find_tree(loop.root)
	return mt.get_nakama_session() if mt != null else null


func _find_tree(node: Node) -> MultiplayerTree:
	if node is MultiplayerTree:
		return node
	for child in node.get_children():
		var found := _find_tree(child)
		if found != null:
			return found
	return null


func _collection(table: StringName) -> String:
	return "%s.%s.%s" % [app_id, _slot, table]


func _slot_index_collection() -> String:
	return "%s.%s" % [app_id, _SLOT_INDEX_SUFFIX]


# Serializes a record into a JSON envelope wrapping a base64 Variant, so Godot
# types (Vector2, colors) survive Nakama's JSON-only storage.
func _encode(record: Dictionary) -> String:
	return JSON.stringify({ "v": Marshalls.variant_to_base64(record) })


# Reverses _encode. The wrapper has already JSON-parsed the stored value.
func _decode(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY or not value.has("v"):
		return { }
	var decoded: Variant = Marshalls.base64_to_variant(String(value["v"]))
	return decoded if typeof(decoded) == TYPE_DICTIONARY else { }


func _cache_table(table: StringName) -> Dictionary:
	if not _cache.has(table):
		_cache[table] = { }
	return _cache[table]


func _mark(sets: Dictionary, table: StringName, id: StringName) -> void:
	if not sets.has(table):
		sets[table] = { }
	sets[table][id] = true


func _merge(into: Dictionary, from: Dictionary) -> void:
	for table in from:
		for id in from[table]:
			_mark(into, table, id)


func _queue_empty() -> bool:
	for table in _dirty:
		if not _dirty[table].is_empty():
			return false
	for table in _deletes:
		if not _deletes[table].is_empty():
			return false
	return true


func _matches_filter(record: Dictionary, filter: Dictionary) -> bool:
	for key in filter:
		if not record.has(key) or record[key] != filter[key]:
			return false
	return true
