## Server-sided persistence surface for one [MultiplayerTree], owned by
## [NetwMultiplayer].
##
## The session always has one. It hosts one
## [NetwPersistenceEngine] per spawned entity whose archetype
## declared [method Netw.configure_persistence], runs a single server-side
## snapshot loop over every engine, and owns the graceful-shutdown drain. The
## database only ever sees what the server sees, so persistence adds no wire
## format: every flush reads
## server-side live values and every hydrate applies on the server and propagates
## through the ordinary spawn and sync machinery. On clients the interface exists
## but registers nothing, because every trigger is server-gated.
## [codeblock]
## PersistenceCore (server)
##  ┠╴ engine per entity   frozen persisted-column set, per-column accumulators
##  ┠╴ snapshot loop        one tick advances every engine, flushes the due subset
##  ┖╴ shutdown drain       broadcast, flush all engines, drain backends, quit
## [/codeblock]
## The entity facade hands one engine out through [member NetwEntity.persistence].
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef

# One engine per persistence-declaring entity, keyed by the RefCounted NetwEntity.
var _engines := NetwPersistenceBook.new()

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


## Returns the [NetwPersistenceEngine] for [param entity], building it the first time
## a persistence-declaring entity is seen, or [code]null[/code] when its archetype
## declares no persistence. The engine is registered into the snapshot loop and a
## final flush is scheduled for when the owner leaves the tree.
func engine_for(entity: NetwEntity) -> NetwPersistenceEngine:
	if not entity or not is_instance_valid(entity.owner):
		return null
	var existing := _engines.engine_of(entity.rid) as NetwPersistenceEngine
	if existing:
		return existing
	var config := NetwScriptModel.get_persistence_config(entity.owner)
	# A scriptless instance declares persistence through metadata instead of a
	# script config, the pack-surviving path the test builders use.
	if not config and (
			entity.owner.has_meta(NetwPersistenceEngine.META_DATABASE)
			or entity.owner.has_meta(NetwPersistenceEngine.META_COLUMNS)):
		config = NetwScriptModel.PersistenceConfig.new()
	if not config:
		return null
	var engine := NetwPersistenceEngine.new(entity, config)
	if engine.columns_empty():
		Netw.dbg.warn(
			"configure_persistence on '%s' declares no persisted field. "
			+ "Mark a field with configure_property(...).persisted().",
			[entity.owner.name],
			func(m): push_warning(m),
		)
	_engines.enroll(entity.rid, engine)
	_lint_engine(engine)
	_arm_shutdown_guard()
	if not entity.owner.tree_exiting.is_connected(_on_owner_exiting):
		entity.owner.tree_exiting.connect(
			_on_owner_exiting.bind(entity),
			CONNECT_ONE_SHOT,
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
	var engine := _engines.engine_of(entity.rid) as NetwPersistenceEngine
	if engine and _is_server():
		engine.flush()
	_engines.drop(entity.rid)


## Flushes every registered engine immediately. Server only in effect. Used before
## a graceful disconnect so no snapshot window is dropped.
func flush_all() -> void:
	if not _is_server():
		return
	for rid: RID in _engines.entities():
		var engine := _engines.engine_of(rid) as NetwPersistenceEngine
		if engine and engine.owner_node():
			engine.flush()


## Advances every engine's per-column accumulators by [param delta] and writes
## the due subset as one transaction per database. Server only in effect. Driven
## by [method MultiplayerTree._process].
##
## The grouping is what makes the node path batch: the gather cost is unchanged,
## because Godot has no batch [method Object.get], but two hundred entities
## sharing one database cost one commit per tick instead of two hundred.
##
## The write goes through [method NetwDatabase.transaction_promise] and reports
## back through the promise rather than being awaited here, which is the form a
## caller that cannot suspend inside a pump needs.
func tick(delta: float) -> void:
	if not _is_server():
		return
	var engines: Array[NetwPersistenceEngine] = []
	var due_rows: Array[Dictionary] = []
	for rid: RID in _engines.entities():
		var engine := _engines.engine_of(rid) as NetwPersistenceEngine
		if engine == null or engine.owner_node() == null:
			_engines.drop(rid)
			continue
		var due: Dictionary = engine.snapshot_tick(delta)
		if due.is_empty():
			continue
		engines.append(engine)
		due_rows.append(due)
	for batch: PackedInt32Array in NetwPersistenceBook.group_by_database(
			due_rows,
	):
		var db: NetwDatabase = due_rows[batch[0]][&"db"]
		db.transaction_promise(
			func(tx: NetwDatabase.TransactionContext) -> void:
				for at: int in batch:
					var due: Dictionary = due_rows[at]
					tx.queue_upsert(
						due[&"table"],
						due[&"id"],
						due[&"values"],
					)
		).then(
			func(result: Variant) -> void:
				if int(result) != OK:
					return
				for at: int in batch:
					engines[at].commit_snapshot(due_rows[at][&"values"])
		)


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
		api.session.notify_shutdown("Server is shutting down.")
		if scene_tree:
			await scene_tree.create_timer(shutdown_notify_delay).timeout
	var drained: Dictionary[NetwDatabase, bool] = { }
	for rid: RID in _engines.entities():
		var engine := _engines.engine_of(rid) as NetwPersistenceEngine
		if engine and engine.owner_node():
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
		await db.backend.drain()


# Warns at schema freeze when a persisted client-owned field is either double
# authority (L1) or has no delivery that reaches the server (L2).
func _lint_engine(engine: NetwPersistenceEngine) -> void:
	engine._lint(_api())
