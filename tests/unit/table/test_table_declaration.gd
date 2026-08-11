## Binding laws for the flat [NetwMultiplayer] table surface.
##
## No session, no wire, no nodes. A table declares nothing of its own, so what
## is proven here is what binding a schema costs and refuses: the wire hash is
## the schema's shape hash verbatim, an unsealed or self-describing schema is
## refused, and one schema name has one table.
class_name TestTableDeclaration
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api


# Declares a schema from [key, type, stride?] rows and seals it.
func _schema(name: StringName, specs: Array) -> RID:
	var schema := api.schema_create(name)
	for spec: Array in specs:
		api.schema_add_column(
			schema,
			spec[0],
			spec[1],
			spec[2] if spec.size() > 2 else 1,
		)
	api.schema_seal(schema)
	return schema


func _mobs() -> RID:
	return _schema(
		&"DeclMob",
		[
			[&"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3],
			[&"hp", NetwMultiplayer.ColumnType.COLUMN_U16],
		],
	)


## Verify a table is a binding and reflection stays with the declaration, so a
## column has one address wherever it is read.
func test_a_table_binds_the_schema_it_reflects_through() -> void:
	var schema := _mobs()
	var table := api.table_create(schema)

	assert_bool(table.is_valid()).is_true()
	assert_that(api.table_get_schema(table)).is_equal(schema)
	assert_int(api.schema_get_column_count(schema)).is_equal(2)
	assert_str(api.schema_get_column_key(schema, 1)).is_equal("hp")


## Verify the wire hash is the shape hash verbatim, which is what keeps every
## TABLE frame carrying the bytes it carried before the declaration moved out
## of the table.
func test_the_wire_hash_is_the_shape_hash() -> void:
	var schema := _mobs()
	var table := api.table_create(schema)

	assert_int(api.table_get_wire_hash(table)).is_equal(
		api.schema_get_hash(schema),
	)
	assert_int(api.table_get_wire_hash(table)).is_not_equal(0)


## Verify a param is local configuration rather than shape, so setting one
## after the binding never moves the wire hash.
func test_a_param_never_enters_the_wire_hash() -> void:
	var schema := _mobs()
	var table := api.table_create(schema)
	var before := api.table_get_wire_hash(table)

	api.table_set_param(table, NetwMultiplayer.TableParam.TABLE_PARAM_RELIABLE, true)

	assert_bool(api._table_core.is_reliable(table)).is_true()
	assert_int(api.table_get_wire_hash(table)).is_equal(before)


## Verify a schema holding the self-describing tier cannot become a table,
## because variable width has no memcpy, no rows-per-frame budget, and no port.
func test_a_variant_schema_cannot_create_a_table() -> void:
	var schema := api.schema_create(&"DeclVariant")
	api.schema_add_column(schema, &"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3)
	api.schema_add_column(schema, &"blob", NetwMultiplayer.ColumnType.COLUMN_VARIANT)
	api.schema_seal(schema)

	var table := api.table_create(schema)

	assert_bool(table.is_valid()).is_false()
	assert_bool(api.table_find(&"DeclVariant").is_valid()).is_false()


## Verify an unsealed schema cannot become a table, since a declaration that
## could still gain a column has no wire order to publish in.
func test_an_unsealed_schema_cannot_create_a_table() -> void:
	var schema := api.schema_create(&"DeclUnsealed")
	api.schema_add_column(schema, &"hp", NetwMultiplayer.ColumnType.COLUMN_U16)

	assert_bool(api.table_create(schema).is_valid()).is_false()

	api.schema_seal(schema)

	assert_bool(api.table_create(schema).is_valid()).is_true()


## Verify binding the same schema twice returns the handle it already has, so a
## script reload finds its own table rather than minting a second one.
func test_binding_twice_returns_the_same_handle() -> void:
	var schema := _mobs()
	var first := api.table_create(schema)

	assert_that(api.table_create(schema)).is_equal(first)
	assert_that(api.table_find(&"DeclMob")).is_equal(first)


## Verify names are the user vocabulary and the handle is the machine key.
func test_table_find_answers_the_schema_name() -> void:
	var table := api.table_create(_mobs())

	assert_that(api.table_find(&"DeclMob")).is_equal(table)
	assert_bool(api.table_find(&"NeverDeclared").is_valid()).is_false()
	assert_bool(api.table_create(RID()).is_valid()).is_false()


## Verify a zero-column table is a tag: it binds, it has a hash, and its
## commits carry membership alone.
func test_a_zero_column_table_is_a_tag() -> void:
	var tag := api.table_create(_schema(&"DeclTag", []))

	assert_bool(tag.is_valid()).is_true()
	assert_int(api.schema_get_column_count(api.table_get_schema(tag))).is_equal(0)
	assert_int(api.table_get_wire_hash(tag)).is_not_equal(0)

	assert_int(api.table_write_routes(tag, PackedInt64Array([7, 9]))).is_equal(OK)
	assert_int(api.table_commit(tag)).is_equal(OK)
	assert_array(api.table_read_routes(tag)).is_equal(PackedInt64Array([7, 9]))


## Verify every verb answers a handle that names no table without reaching
## into a null record.
func test_an_unknown_handle_is_answered_rather_than_crashed() -> void:
	var bogus := RID()

	assert_int(api.table_get_wire_hash(bogus)).is_equal(0)
	assert_bool(api.table_get_schema(bogus).is_valid()).is_false()
	assert_int(
		api.table_write_routes(bogus, PackedInt64Array([1])),
	).is_equal(ERR_DOES_NOT_EXIST)
	assert_int(api.table_commit(bogus)).is_equal(ERR_DOES_NOT_EXIST)
	assert_int(api.table_get_row(bogus, 1)).is_equal(-1)
	assert_int(api.table_get_tick(bogus)).is_equal(-1)
	assert_array(api.table_read_routes(bogus)).is_empty()
	assert_object(api.table_read_column(bogus, 0)).is_null()
