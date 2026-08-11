## Declaration laws for the flat [NetwMultiplayer] schema surface.
##
## No session, no wire, no nodes, no store. What is proven here is that sealing
## fixes the column address, that the shape hash is a real skew check over
## everything a binding could disagree about, and that a script reload replaying
## its own declaration is idempotent rather than a second schema.
class_name TestSchemaDeclaration
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api


func _mobs() -> RID:
	var schema := api.schema_create(&"SchemaMob")
	api.schema_add_column(schema, &"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3)
	api.schema_add_column(schema, &"hp", NetwMultiplayer.ColumnType.COLUMN_U16)
	api.schema_seal(schema)
	return schema


## Verify the flat column enum mirrors the core it compiles into, so a value
## written against one surface addresses the same storage on the other, and
## that the self-describing tier is appended rather than inserted.
func test_column_type_mirrors_the_core() -> void:
	assert_int(NetwMultiplayer.ColumnType.COLUMN_F32).is_equal(
		SchemaCore.ColumnType.F32,
	)
	assert_int(NetwMultiplayer.ColumnType.COLUMN_ENTITY).is_equal(
		SchemaCore.ColumnType.ENTITY,
	)
	assert_int(NetwMultiplayer.ColumnType.COLUMN_VARIANT).is_equal(
		SchemaCore.ColumnType.VARIANT,
	)
	assert_int(SchemaCore.ColumnType.VARIANT).is_equal(
		SchemaCore.ColumnType.ENTITY + 1,
	)
	assert_int(SchemaCore.storage_type(SchemaCore.ColumnType.VARIANT)).is_equal(
		TYPE_ARRAY,
	)
	assert_int(SchemaCore.element_type(SchemaCore.ColumnType.VARIANT)).is_equal(
		TYPE_NIL,
	)
	assert_int(
		SchemaCore.storage_type(SchemaCore.ColumnType.VARIANT + 1),
	).is_equal(-1)
	assert_int(
		SchemaCore.element_type(SchemaCore.ColumnType.VARIANT + 1),
	).is_equal(-1)


## Verify declaration order is the column address, and reflection answers the
## whole shape a generic walker needs.
func test_declaration_order_is_the_column_address() -> void:
	var schema := api.schema_create(&"SchemaOrder")
	var pos := api.schema_add_column(
		schema,
		&"pos",
		NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
	)
	var cooldown := api.schema_add_column(
		schema,
		&"cooldown",
		NetwMultiplayer.ColumnType.COLUMN_F32,
		8,
	)

	assert_int(pos).is_equal(0)
	assert_int(cooldown).is_equal(1)
	assert_int(api.schema_get_column_count(schema)).is_equal(2)
	assert_str(api.schema_get_column_key(schema, 1)).is_equal("cooldown")
	assert_int(api.schema_get_column_type(schema, 1)).is_equal(
		NetwMultiplayer.ColumnType.COLUMN_F32,
	)
	assert_int(api.schema_get_column_stride(schema, 1)).is_equal(8)
	assert_int(api.schema_get_column_stride(schema, 0)).is_equal(1)
	assert_str(api.schema_get_column_key(schema, 7)).is_equal("")
	assert_int(api.schema_get_column_stride(schema, 7)).is_equal(0)


## Verify the self-describing tier is declarable, since a [String] property has
## nowhere else to go and the database and the property binding both take it.
func test_a_variant_column_is_declarable() -> void:
	var schema := api.schema_create(&"SchemaVariant")
	var blob := api.schema_add_column(
		schema,
		&"blob",
		NetwMultiplayer.ColumnType.COLUMN_VARIANT,
	)

	assert_int(blob).is_equal(0)
	assert_int(api.schema_get_column_type(schema, blob)).is_equal(
		NetwMultiplayer.ColumnType.COLUMN_VARIANT,
	)
	assert_int(api.schema_seal(schema)).is_equal(OK)
	assert_bool(api._schema_core.has_variant(schema)).is_true()


## Verify a duplicate key and a malformed shape are refused at declaration
## rather than at a binding, where two peers would already disagree.
func test_malformed_columns_are_refused() -> void:
	var schema := api.schema_create(&"SchemaMalformed")
	api.schema_add_column(schema, &"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3)

	assert_int(
		api.schema_add_column(
			schema,
			&"pos",
			NetwMultiplayer.ColumnType.COLUMN_F32,
		),
	).is_equal(-1)
	assert_int(
		api.schema_add_column(schema, &"", NetwMultiplayer.ColumnType.COLUMN_F32),
	).is_equal(-1)
	assert_int(
		api.schema_add_column(
			schema,
			&"zero",
			NetwMultiplayer.ColumnType.COLUMN_F32,
			0,
		),
	).is_equal(-1)


## Verify sealing freezes the declaration, because a schema that could still
## gain a column has no stable address to hand out.
func test_seal_freezes_the_declaration() -> void:
	var schema := _mobs()

	assert_int(
		api.schema_add_column(
			schema,
			&"late",
			NetwMultiplayer.ColumnType.COLUMN_F32,
		),
	).is_equal(-1)
	assert_int(api.schema_get_column_count(schema)).is_equal(2)


## Verify the shape hash is zero until sealing fixes it, then stable, and that
## it moves when any visible part of the shape moves.
func test_the_shape_hash_is_a_skew_check() -> void:
	var schema := api.schema_create(&"SchemaHash")
	api.schema_add_column(schema, &"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3)
	assert_int(api.schema_get_hash(schema)).is_equal(0)

	api.schema_seal(schema)
	var sealed := api.schema_get_hash(schema)
	assert_int(sealed).is_not_equal(0)
	assert_int(sealed).is_less_equal(0xFFFF)
	assert_int(api.schema_get_hash(schema)).is_equal(sealed)

	var wider := api.schema_create(&"SchemaHashWider")
	api.schema_add_column(
		wider,
		&"pos",
		NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
		2,
	)
	api.schema_seal(wider)
	assert_int(api.schema_get_hash(wider)).is_not_equal(sealed)

	var retyped := api.schema_create(&"SchemaHashRetyped")
	api.schema_add_column(retyped, &"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR2)
	api.schema_seal(retyped)
	assert_int(api.schema_get_hash(retyped)).is_not_equal(sealed)


## Verify a quantizer is part of the sealed shape, because two peers that
## packed one column to different widths cannot read each other.
func test_the_shape_hash_covers_the_quantizer() -> void:
	var raw := api.schema_create(&"SchemaQuantRaw")
	api.schema_add_column(raw, &"pos", NetwMultiplayer.ColumnType.COLUMN_VECTOR3)
	api.schema_seal(raw)

	var packed := api.schema_create(&"SchemaQuantPacked")
	var column := api.schema_add_column(
		packed,
		&"pos",
		NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
	)
	api.schema_set_column_quantizer(
		packed,
		column,
		NetwQuantizeFixed.new().step(0.03).limits(-512.0, 512.0),
	)
	api.schema_seal(packed)

	assert_int(api.schema_get_hash(packed)).is_not_equal(
		api.schema_get_hash(raw),
	)


## Verify the shape hash is the value a TABLE frame carries, so the one
## formula the wire depends on has one owner.
func test_the_shape_hash_is_the_table_wire_hash() -> void:
	var schema := api.schema_create(&"SchemaParity")
	var column := api.schema_add_column(
		schema,
		&"pos",
		NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
	)
	api.schema_set_column_quantizer(
		schema,
		column,
		NetwQuantizeFixed.new().step(0.03).limits(-512.0, 512.0),
	)
	api.schema_seal(schema)

	var table := api.table_create(schema)

	assert_int(api.table_get_wire_hash(table)).is_equal(
		api.schema_get_hash(schema),
	)


## Verify re-declaring a sealed schema replays it, which is what a script
## reload does, and returns the indices user code already holds.
func test_redeclaring_the_same_shape_is_idempotent() -> void:
	var first := _mobs()
	var again := api.schema_create(&"SchemaMob")

	assert_that(again).is_equal(first)
	assert_int(
		api.schema_add_column(
			again,
			&"pos",
			NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
		),
	).is_equal(0)
	assert_int(
		api.schema_add_column(again, &"hp", NetwMultiplayer.ColumnType.COLUMN_U16),
	).is_equal(1)
	assert_int(api.schema_seal(again)).is_equal(OK)
	assert_int(api.schema_get_column_count(again)).is_equal(2)


## Verify a changed re-declaration fails loudly instead of silently shifting
## every column address.
func test_redeclaring_a_different_shape_is_refused() -> void:
	_mobs()
	var again := api.schema_create(&"SchemaMob")

	assert_int(
		api.schema_add_column(
			again,
			&"pos",
			NetwMultiplayer.ColumnType.COLUMN_VECTOR2,
		),
	).is_equal(-1)
	assert_int(api.schema_seal(again)).is_equal(ERR_UNCONFIGURED)


## Verify a re-declaration that stops short of the sealed column list is
## refused too, since a shorter replay is as much a disagreement as a changed
## one.
func test_a_truncated_redeclaration_is_refused() -> void:
	_mobs()
	var again := api.schema_create(&"SchemaMob")

	assert_int(
		api.schema_add_column(
			again,
			&"pos",
			NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
		),
	).is_equal(0)
	assert_int(api.schema_seal(again)).is_equal(ERR_UNCONFIGURED)


## Verify names are the user vocabulary and the handle is the machine key.
func test_schema_find_answers_the_declared_name() -> void:
	var schema := _mobs()

	assert_that(api.schema_find(&"SchemaMob")).is_equal(schema)
	assert_bool(api.schema_find(&"NeverDeclared").is_valid()).is_false()
	assert_bool(api.schema_create(&"").is_valid()).is_false()


## Verify a zero-column schema is legal, which is what makes a tag table a
## table rather than a second mechanism.
func test_a_zero_column_schema_seals() -> void:
	var tag := api.schema_create(&"SchemaTag")

	assert_int(api.schema_seal(tag)).is_equal(OK)
	assert_int(api.schema_get_column_count(tag)).is_equal(0)
	assert_int(api.schema_get_hash(tag)).is_not_equal(0)


## Verify every verb answers a handle that names no schema without reaching
## into a null record.
func test_an_unknown_handle_is_answered_rather_than_crashed() -> void:
	var bogus := RID()

	assert_int(api.schema_get_column_count(bogus)).is_equal(0)
	assert_int(api.schema_get_hash(bogus)).is_equal(0)
	assert_int(api.schema_seal(bogus)).is_equal(ERR_DOES_NOT_EXIST)
	assert_str(api.schema_get_column_key(bogus, 0)).is_equal("")
	assert_int(api.schema_get_column_stride(bogus, 0)).is_equal(0)
	assert_int(
		api.schema_add_column(bogus, &"x", NetwMultiplayer.ColumnType.COLUMN_F32),
	).is_equal(-1)
