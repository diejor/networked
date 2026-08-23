## Publish and consume laws for the flat [NetwMultiplayer] table surface.
##
## The two contracts under test are the ones the whole design rests on: a
## commit copies, so the caller's arrays are theirs again, and a read does not,
## so consuming a two thousand row column is free.
class_name TestTablePublish
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer
var mobs: RID
var pos: int
var hp: int


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	pos = 0
	hp = 1
	mobs = _bind(
		&"PubMob",
		[
			[&"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3],
			[&"hp", NetwMultiplayer.ColumnType.COLUMN_U16],
		],
	)


# Declares a schema from [key, type, stride?] rows and binds a table to it.
func _bind(name: StringName, specs: Array) -> RID:
	var schema := api.schema_create(name)
	for spec: Array in specs:
		api.schema_add_column(
			schema,
			spec[0],
			spec[1],
			spec[2] if spec.size() > 2 else 1,
		)
	api.schema_seal(schema)
	return api.table_create(schema)


func _publish(
		routes: PackedInt64Array,
		positions: PackedVector3Array,
		hits: PackedInt32Array,
) -> Error:
	api.table_write_routes(mobs, routes)
	api.table_write_column(mobs, pos, positions)
	api.table_write_column(mobs, hp, hits)
	return api.table_commit(mobs)


## Verify a whole commit becomes the applied state and reads answer it.
func test_a_commit_becomes_the_applied_state() -> void:
	var verdict := _publish(
		PackedInt64Array([4, 5, 6]),
		PackedVector3Array([Vector3.ZERO, Vector3.UP, Vector3.RIGHT]),
		PackedInt32Array([100, 90, 80]),
	)

	assert_int(verdict).is_equal(OK)
	assert_array(api.table_read_routes(mobs)).is_equal(PackedInt64Array([4, 5, 6]))
	assert_array(api.table_read_column(mobs, hp)).is_equal(
		PackedInt32Array([100, 90, 80]),
	)
	assert_array(api.table_read_column(mobs, pos)).is_equal(
		PackedVector3Array([Vector3.ZERO, Vector3.UP, Vector3.RIGHT]),
	)


## Verify the commit snapshots, so a caller mutating its own arrays on the next
## line cannot tear the published state. Packed arrays are shared references in
## GDScript, so nothing but an explicit copy buys this.
func test_a_commit_snapshots_so_the_caller_keeps_its_arrays() -> void:
	var routes := PackedInt64Array([1, 2])
	var positions := PackedVector3Array([Vector3.ZERO, Vector3.ONE])
	var hits := PackedInt32Array([10, 20])
	_publish(routes, positions, hits)

	positions[0] = Vector3(99.0, 99.0, 99.0)
	hits[1] = -1
	routes.append(3)

	assert_array(api.table_read_column(mobs, pos)).is_equal(
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
	)
	assert_array(api.table_read_column(mobs, hp)).is_equal(
		PackedInt32Array([10, 20]),
	)
	assert_array(api.table_read_routes(mobs)).is_equal(PackedInt64Array([1, 2]))


## Verify a read hands back the store itself, which is what makes reading free
## and makes duplicate() the documented way to keep history.
func test_a_read_is_a_live_view() -> void:
	_publish(
		PackedInt64Array([1]),
		PackedVector3Array([Vector3.ZERO]),
		PackedInt32Array([7]),
	)

	var first: PackedVector3Array = api.table_read_column(mobs, pos)
	var second: PackedVector3Array = api.table_read_column(mobs, pos)
	first[0] = Vector3.UP
	assert_vector(second[0]).is_equal(Vector3.UP)

	var kept: PackedVector3Array = api.table_read_column(mobs, pos).duplicate()
	first[0] = Vector3.DOWN
	assert_vector(kept[0]).is_equal(Vector3.UP)


## Verify a half table can never reach the wire: every column must be written
## and every length must agree with the row count.
func test_a_half_commit_is_refused() -> void:
	api.table_write_routes(mobs, PackedInt64Array([1, 2]))
	assert_int(api.table_commit(mobs)).is_equal(ERR_INVALID_DATA)

	api.table_write_column(
		mobs,
		pos,
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
	)
	assert_int(api.table_commit(mobs)).is_equal(ERR_INVALID_DATA)

	api.table_write_column(mobs, hp, PackedInt32Array([1]))
	assert_int(api.table_commit(mobs)).is_equal(ERR_INVALID_DATA)

	api.table_write_column(mobs, hp, PackedInt32Array([1, 2]))
	assert_int(api.table_commit(mobs)).is_equal(OK)


## Verify a column refuses a buffer of the wrong storage class, since the
## storage class is what the wire layout is computed from.
func test_a_column_refuses_the_wrong_storage_class() -> void:
	assert_int(
		api.table_write_column(mobs, pos, PackedFloat32Array([1.0])),
	).is_equal(ERR_INVALID_DATA)
	assert_int(
		api.table_write_column(mobs, 9, PackedInt32Array([1])),
	).is_equal(ERR_INVALID_DATA)


## Verify a fixed-capacity array is a stride rather than a second type family,
## and that its length rule is rows times stride.
func test_stride_is_how_a_fixed_capacity_array_is_expressed() -> void:
	var cooldown := 0
	var table := _bind(
		&"PubStride",
		[[&"cooldown", NetwMultiplayer.ColumnType.COLUMN_F32, 4]],
	)

	api.table_write_routes(table, PackedInt64Array([1, 2]))
	api.table_write_column(table, cooldown, PackedFloat32Array([1.0, 2.0, 3.0]))
	assert_int(api.table_commit(table)).is_equal(ERR_INVALID_DATA)

	api.table_write_column(
		table,
		cooldown,
		PackedFloat32Array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]),
	)
	assert_int(api.table_commit(table)).is_equal(OK)
	assert_int(api.table_read_column(table, cooldown).size()).is_equal(8)


## Verify cohorts are derived by comparing one wave against the last, so a
## consumer learns what appeared and what left without either riding the wire.
func test_births_and_deaths_are_derived_from_the_wave() -> void:
	_publish(
		PackedInt64Array([1, 2]),
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
		PackedInt32Array([1, 2]),
	)
	assert_array(api.table_read_births(mobs)).is_equal(PackedInt64Array([1, 2]))
	assert_array(api.table_read_deaths(mobs)).is_empty()

	_publish(
		PackedInt64Array([2, 3]),
		PackedVector3Array([Vector3.ONE, Vector3.UP]),
		PackedInt32Array([2, 3]),
	)
	assert_array(api.table_read_births(mobs)).is_equal(PackedInt64Array([3]))
	assert_array(api.table_read_deaths(mobs)).is_equal(PackedInt64Array([1]))

	_publish(
		PackedInt64Array([2, 3]),
		PackedVector3Array([Vector3.ONE, Vector3.UP]),
		PackedInt32Array([9, 9]),
	)
	assert_array(api.table_read_births(mobs)).is_empty()
	assert_array(api.table_read_deaths(mobs)).is_empty()


## Verify the row map answers one route and a whole join, and that the bulk
## form is what a join uses so a hot loop makes one crossing rather than n.
func test_the_row_map_answers_one_route_and_a_whole_join() -> void:
	_publish(
		PackedInt64Array([10, 20, 30]),
		PackedVector3Array([Vector3.ZERO, Vector3.ONE, Vector3.UP]),
		PackedInt32Array([1, 2, 3]),
	)

	assert_int(api.table_get_row(mobs, 20)).is_equal(1)
	assert_int(api.table_get_row(mobs, 99)).is_equal(-1)
	assert_array(
		api.table_get_rows(mobs, PackedInt64Array([30, 99, 10])),
	).is_equal(PackedInt32Array([2, -1, 0]))
	assert_array(api.table_get_rows(mobs, PackedInt64Array())).is_empty()


## Verify a route from anywhere is writable, which is what lets a component
## table key on the routes of the table it refines.
func test_a_foreign_route_is_writable() -> void:
	var entity := api.entity_create()
	var route := api.entity_admit(entity)
	var claimed := api.claim_routes(1)

	var verdict := _publish(
		PackedInt64Array([route, claimed[0]]),
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
		PackedInt32Array([5, 6]),
	)

	assert_int(verdict).is_equal(OK)
	assert_int(api.table_get_row(mobs, route)).is_equal(0)
	assert_int(api.table_get_row(mobs, claimed[0])).is_equal(1)


## Verify each commit stamps the clock's tick, which is the token freshness is
## judged by, and that a session with no clock still publishes locally.
func test_a_commit_carries_the_session_tick() -> void:
	assert_int(api.table_get_tick(mobs)).is_equal(-1)

	_publish(
		PackedInt64Array([1]),
		PackedVector3Array([Vector3.ZERO]),
		PackedInt32Array([1]),
	)
	assert_int(api.table_get_tick(mobs)).is_equal(api._native_core.clock_handle.tick)


## Verify a commit marks the table for the tick boundary and clears once its
## frames have left, so several commits in one tick collapse to the last.
func test_a_commit_marks_the_table_for_the_tick_boundary() -> void:
	assert_array(api._table_core.dirty_tables()).is_empty()

	_publish(
		PackedInt64Array([1]),
		PackedVector3Array([Vector3.ZERO]),
		PackedInt32Array([1]),
	)
	_publish(
		PackedInt64Array([1]),
		PackedVector3Array([Vector3.ONE]),
		PackedInt32Array([2]),
	)
	var dirty := api._table_core.dirty_tables()
	assert_int(dirty.size()).is_equal(1)
	assert_that(dirty[0]).is_equal(mobs)

	api._table_core.clear_dirty(mobs)
	assert_array(api._table_core.dirty_tables()).is_empty()


## Verify session teardown drops the rows and keeps the declaration, so a
## re-entered session finds the same tables under the same handles.
func test_session_teardown_keeps_the_declaration_and_drops_the_rows() -> void:
	_publish(
		PackedInt64Array([1, 2]),
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
		PackedInt32Array([1, 2]),
	)

	api._clear_flat_family_state()

	assert_that(api.table_find(&"PubMob")).is_equal(mobs)
	assert_int(
		api.schema_get_column_count(api.table_get_schema(mobs)),
	).is_equal(2)
	assert_int(api.table_get_wire_hash(mobs)).is_not_equal(0)
	assert_array(api.table_read_routes(mobs)).is_empty()
	assert_int(api.table_get_tick(mobs)).is_equal(-1)
	assert_int(api.table_get_row(mobs, 1)).is_equal(-1)
	assert_array(api._table_core.dirty_tables()).is_empty()

	assert_int(
		_publish(
			PackedInt64Array([3]),
			PackedVector3Array([Vector3.UP]),
			PackedInt32Array([3]),
		),
	).is_equal(OK)
