## The GDScript half of the session's persistence plane: the archetype readers
## [NetwPersistenceEngine] reads its declaration through, and the graceful
## shutdown drain.
##
## Everything else about persistence is native. What is left here is what the
## engine has no spelling for: the declaration models a game writes against
## ([NetwScriptModel], [NetwSchemaModel], [NetwPropertySet]), and a drain that
## has to WAIT on a [SceneTree] timer and on the database, which is the one act
## no bound method can carry.
## [codeblock]
## persistence (server)
##  ┠╴ readers          archetype -> declaration, node -> columns, schema
##  ┠╴ snapshot loop    native; one tick advances every engine
##  ┖╴ shutdown drain   broadcast, grace window, flush all, drain, quit
## [/codeblock]
## The entity facade hands one engine out through
## [member NetwEntity.persistence].
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef

## Seconds the server waits after broadcasting the shutdown notice before saving
## and quitting, giving clients time to react before the connection drops.
var shutdown_notify_delay: float = 0.5


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	_install_model_readers()
	var core := _core()
	if core:
		core.set_persistence_quit_guard(_arm_shutdown_guard)
		core.set_persistence_drain(_drain_and_quit)


# The three archetype declarations [NetwPersistenceEngine] reads, each of which
# names a GDScript model the engine has no spelling for.
static func _install_model_readers() -> void:
	NetwPersistenceEngine.set_config_reader(_config_of)
	NetwPersistenceEngine.set_property_configs_reader(
		NetwScriptModel.get_node_property_configs,
	)
	NetwPersistenceEngine.set_schema_declarer(_declare_schema)


# The archetype's persistence declaration, flattened to the five facts the
# engine freezes. An empty answer means the archetype declares no persistence.
# A scriptless instance declares it through metadata instead of a script config,
# the pack-surviving path the test builders use.
static func _config_of(owner: Node) -> Dictionary:
	var config := NetwScriptModel.get_persistence_config(owner)
	if not config and (
			owner.has_meta(NetwPersistenceEngine.meta_database())
			or owner.has_meta(NetwPersistenceEngine.meta_columns())):
		config = NetwScriptModel.PersistenceConfig.new()
	if not config:
		return { }
	return {
		&"default_interval": config.default_interval,
		&"database": config.db,
		&"table": config.table_name,
		&"record_id_provider": config.record_id_provider,
		&"hydrate_on_spawn": config.hydrate_on_spawn_enabled,
	}


# The declaration carries the column types, reflected through the node that
# owns each property, which is what lets the database reject a stored value of
# the wrong shape instead of assigning it.
static func _declare_schema(
		db: NetwDatabase,
		table: StringName,
		columns: Array,
) -> void:
	var declaration := NetwSchemaModel.Declaration.new(table)
	declaration.replicated = false
	for column: Dictionary in columns:
		var property: StringName = column.get(&"property", &"")
		var node := column.get(&"node") as Node
		var script := node.get_script() as Script if is_instance_valid(node) \
				else null
		declaration.column(
			property,
			NetwPropertySet.column_type_for(script, node, property),
			1,
			null,
		)
	db.declare_table(table, declaration)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


func _core() -> NetwMultiplayerCore:
	var api := _api()
	return api._native_core if api else null


# Blocks the auto-quit on a host with persisted entities so the window-close
# notification can run the graceful drain before the process exits.
func _arm_shutdown_guard() -> void:
	var api := _api()
	var scene_tree := Engine.get_main_loop() as SceneTree
	if api and api.is_host and scene_tree:
		scene_tree.set_auto_accept_quit(false)


# Broadcasts the shutdown notice, waits out the grace window, flushes every live
# engine, drains the write-behind backends, then quits. The one persistence act
# that must COMPLETE rather than come due, which is why it waits and why it is
# still written here.
func _drain_and_quit() -> void:
	var api := _api()
	var core := _core()
	var scene_tree := Engine.get_main_loop() as SceneTree
	if api and api.is_host:
		api.session.notify_shutdown("Server is shutting down.")
		if scene_tree:
			await scene_tree.create_timer(shutdown_notify_delay).timeout
	var drained: Dictionary[NetwDatabase, bool] = { }
	for engine: NetwPersistenceEngine in core.persistence_live_engines():
		await NetwDatabase.settled_error(engine.flush())
		var db := engine.database() as NetwDatabase
		if db:
			drained[db] = true
	for db in drained:
		await NetwDatabase.settled_error(_drain_database(db))
	if scene_tree:
		scene_tree.quit()


# Drains a write-behind backend (e.g. NakamaDatabase) so its last flush
# window is not lost on quit. A backend with no drain has nothing in flight,
# so it answers OK.
func _drain_database(db: NetwDatabase) -> NetwPromise:
	if not db or not db.backend or not db.backend.has_method("drain"):
		return NetwPromise.resolved(OK)
	return db.backend.drain() as NetwPromise
