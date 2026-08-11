## Round-trip laws for a replicated table at rest.
##
## The unit of commit and of hydrate is the whole table, because routes must be
## re-minted as a set, so a table saves as one record. What is proven here is
## that the values come back byte-identical and unquantized, that a fresh
## session gets fresh routes paired with the caller's own save keys, and that a
## route column is honestly refused rather than silently saved.
class_name TestTablePersistence
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer
var db: NetwDatabase
var backend: TestMemoryBackend


func before_test() -> void:
	NetwSchemaModel.clear()
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	db = auto_free(NetwDatabase.new())
	backend = auto_free(TestMemoryBackend.new())
	db.backend = backend


func after_test() -> void:
	NetwSchemaModel.clear()
	await super.after_test()


# Declares a schema statically, the way a game does, so every session built
# afterwards adopts the same one and binds its own table.
func _declare(name: StringName, specs: Array) -> RID:
	var schema := Netw.configure_schema(name)
	for spec: Array in specs:
		match int(spec[1]):
			NetwMultiplayer.ColumnType.COLUMN_VECTOR3:
				schema.vector3(spec[0], spec[2] if spec.size() > 2 else null)
			NetwMultiplayer.ColumnType.COLUMN_U16:
				schema.u16(spec[0])
			NetwMultiplayer.ColumnType.COLUMN_F32:
				schema.f32(spec[0])
			NetwMultiplayer.ColumnType.COLUMN_ENTITY:
				schema.entity(spec[0])
	return api.table_find(name)


func _mobs(name: StringName = &"SaveMob") -> RID:
	return _declare(
		name,
		[
			[
				&"pos",
				NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
				NetwQuantizeFixed.new().step(0.5).limits(-512.0, 512.0),
			],
			[&"hp", NetwMultiplayer.ColumnType.COLUMN_U16],
		],
	)


## Verify a flush and a hydrate in a fresh session agree on every value, and
## that the routes are new while the save keys are the ones the caller wrote.
func test_a_table_round_trips_through_one_record() -> void:
	var table := _mobs()
	var routes := api.claim_routes(3)
	var positions := PackedVector3Array(
		[Vector3(1.0 / 3.0, -2.7, 5.25), Vector3.ZERO, Vector3.UP],
	)
	api.table_write_routes(table, routes)
	api.table_write_column(table, 0, positions)
	api.table_write_column(table, 1, PackedInt32Array([100, 90, 80]))
	assert_int(api.table_commit(table)).is_equal(OK)

	var ids := PackedStringArray(["mob_a", "mob_b", "mob_c"])
	assert_int(
		await api.persist_table_flush(table, db, &"mobs", ids),
	).is_equal(OK)

	var other := MultiplayerTree.new()
	other.name = "LoadTree"
	add_child(other)
	auto_free(other)
	var loaded := other.api.table_find(&"SaveMob")
	assert_bool(loaded.is_valid()).is_true()

	var back := await other.api.persist_table_hydrate(loaded, db, &"mobs")

	assert_array(back[&"ids"]).is_equal(ids)
	assert_int((back[&"routes"] as PackedInt64Array).size()).is_equal(3)
	assert_array(other.api.table_read_routes(loaded)).is_equal(back[&"routes"])
	assert_array(other.api.table_read_column(loaded, 1)).is_equal(
		PackedInt32Array([100, 90, 80]),
	)
	# Unquantized at rest: the wire codec is not on the save path, so a column
	# quantized to half a metre comes back at full precision.
	assert_array(other.api.table_read_column(loaded, 0)).is_equal(positions)


## Verify one table is one record, so two thousand rows are one write rather
## than two thousand.
func test_a_whole_table_is_one_record() -> void:
	var table := _mobs()
	var routes := api.claim_routes(40)
	var positions := PackedVector3Array()
	var hp := PackedInt32Array()
	positions.resize(40)
	hp.resize(40)
	api.table_write_routes(table, routes)
	api.table_write_column(table, 0, positions)
	api.table_write_column(table, 1, hp)
	api.table_commit(table)

	var ids := PackedStringArray()
	ids.resize(40)
	await api.persist_table_flush(table, db, &"mobs", ids)

	assert_int(backend.upsert_calls.size()).is_equal(1)
	assert_that(backend.upsert_calls[0].get("id")).is_equal(&"SaveMob")


## Verify the save keys must be parallel to the committed row order, since a
## pairing that does not line up names the wrong row for the rest of time.
func test_a_mismatched_id_count_is_refused() -> void:
	var table := _mobs()
	api.table_write_routes(table, api.claim_routes(2))
	api.table_write_column(
		table,
		0,
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
	)
	api.table_write_column(table, 1, PackedInt32Array([1, 2]))
	api.table_commit(table)

	assert_int(
		await api.persist_table_flush(
			table,
			db,
			&"mobs",
			PackedStringArray(["only_one"]),
		),
	).is_equal(ERR_INVALID_DATA)
	assert_array(backend.upsert_calls).is_empty()


## Verify a route column is not saved and zero-fills on hydrate, because a
## persisted route is meaningless in the session that loads it.
func test_a_route_column_is_skipped_and_zero_filled() -> void:
	var table := _declare(
		&"SaveEdge",
		[
			[&"target", NetwMultiplayer.ColumnType.COLUMN_ENTITY],
			[&"weight", NetwMultiplayer.ColumnType.COLUMN_F32],
		],
	)
	var routes := api.claim_routes(2)
	api.table_write_routes(table, routes)
	api.table_write_column(table, 0, PackedInt64Array([routes[1], routes[0]]))
	api.table_write_column(table, 1, PackedFloat32Array([1.5, 2.5]))
	api.table_commit(table)

	assert_int(
		await api.persist_table_flush(
			table,
			db,
			&"edges",
			PackedStringArray(["e0", "e1"]),
		),
	).is_equal(OK)
	assert_bool(
		(backend.upsert_calls[0].get("data") as Dictionary).has(&"target"),
	).is_false()

	var other := MultiplayerTree.new()
	other.name = "EdgeTree"
	add_child(other)
	auto_free(other)
	var loaded := other.api.table_find(&"SaveEdge")

	await other.api.persist_table_hydrate(loaded, db, &"edges")

	assert_array(other.api.table_read_column(loaded, 0)).is_equal(
		PackedInt64Array([0, 0]),
	)
	assert_array(other.api.table_read_column(loaded, 1)).is_equal(
		PackedFloat32Array([1.5, 2.5]),
	)


## Verify a first play hydrates to nothing rather than erroring, the same way a
## node entity keeps its scene defaults when no row exists.
func test_a_missing_record_is_a_first_play() -> void:
	var table := _mobs()

	var back := await api.persist_table_hydrate(table, db, &"mobs")

	assert_array(back[&"routes"]).is_empty()
	assert_array(back[&"ids"]).is_empty()
	assert_array(api.table_read_routes(table)).is_empty()
