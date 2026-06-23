## Tests for [WarmPolicy], [WarmRequest], and [NetwDatabase] warm wiring.
##
## Covers the eager-all default, the null-warms-nothing case, per-table scoping,
## and the runtime [method NetwDatabase.warm] escape hatch. Sync backends no-op
## warm, so a spy records the directives the database hands down.
class_name TestWarmPolicy
extends NetwTestSuite

# Records every warm batch so tests can assert what the policy produced.
class WarmSpyBackend extends TestMemoryBackend:
	var warm_calls: Array = []

	func warm(directives: Array) -> Error:
		warm_calls.append(directives)
		return OK

# Warms only the players table, leaving every other table lazy.
class PlayersOnlyPolicy extends WarmPolicy:
	func plan_table(table: StringName, _columns: Array[StringName]) -> WarmRequest:
		return WarmRequest.all() if table == &"players" else WarmRequest.none()


func _make_db() -> NetwDatabase:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(WarmSpyBackend.new())
	return db


func test_warm_request_constructors_carry_scope() -> void:
	assert_int(WarmRequest.none().kind).is_equal(WarmRequest.Kind.NONE)
	assert_int(WarmRequest.all().kind).is_equal(WarmRequest.Kind.ALL)

	var by_ids := WarmRequest.ids([&"a", &"b"])
	assert_int(by_ids.kind).is_equal(WarmRequest.Kind.IDS)
	assert_array(by_ids.id_list).contains_exactly([&"a", &"b"])

	var by_filter := WarmRequest.filter({ &"online": true })
	assert_int(by_filter.kind).is_equal(WarmRequest.Kind.FILTER)
	assert_bool(by_filter.filter_map.get(&"online")).is_true()


func test_eager_policy_warms_every_table() -> void:
	var db := _make_db()
	db._schema[&"players"] = [&"hp"] as Array[StringName]
	db._schema[&"items"] = [&"damage"] as Array[StringName]

	var directives := db._build_warm_directives()
	var tables: Array[StringName] = []
	for directive in directives:
		tables.append(directive.table)
		assert_int(directive.request.kind).is_equal(WarmRequest.Kind.ALL)
	assert_array(tables).contains([&"players", &"items"])


func test_null_policy_warms_nothing() -> void:
	var db := _make_db()
	db.warm_policy = null
	db._schema[&"players"] = [&"hp"] as Array[StringName]
	assert_array(db._build_warm_directives()).is_empty()


func test_custom_policy_scopes_per_table() -> void:
	var db := _make_db()
	db.warm_policy = PlayersOnlyPolicy.new()
	db._schema[&"players"] = [&"hp"] as Array[StringName]
	db._schema[&"items"] = [&"damage"] as Array[StringName]

	var directives := db._build_warm_directives()
	# items returns WarmRequest.none(), so it is filtered out of the batch.
	assert_int(directives.size()).is_equal(1)
	assert_str(directives[0].table).is_equal(&"players")


func test_init_calls_backend_warm_via_policy() -> void:
	var db := _make_db()
	db._register_schema(&"players", [&"hp"])
	await get_tree().process_frame

	var backend := db.backend as WarmSpyBackend
	assert_int(backend.warm_calls.size()).is_equal(1)
	var directives: Array = backend.warm_calls[0]
	assert_int(directives.size()).is_equal(1)
	assert_str(directives[0].table).is_equal(&"players")


func test_runtime_warm_forwards_single_table() -> void:
	var db := _make_db()
	db._register_schema(&"players", [&"hp"])
	await get_tree().process_frame

	var backend := db.backend as WarmSpyBackend
	backend.warm_calls.clear()

	var request := WarmRequest.ids([&"valeria"])
	var err: Error = await db.warm(&"players", request)
	assert_int(err).is_equal(OK)
	assert_int(backend.warm_calls.size()).is_equal(1)
	var directives: Array = backend.warm_calls[0]
	assert_int(directives.size()).is_equal(1)
	assert_str(directives[0].table).is_equal(&"players")
	assert_int(directives[0].request.kind).is_equal(WarmRequest.Kind.IDS)
