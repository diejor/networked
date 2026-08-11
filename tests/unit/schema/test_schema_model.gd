## Declaration laws for [NetwSchemaModel] and the [NetwSchema] builder.
##
## No session and no RIDs. What is proven here is that a static declaration is
## pure data, that a script reload replaying it is idempotent, that a
## declaration says for itself whether it wants a wire, and that the adoption
## order two peers compute is the same one.
class_name TestSchemaModel
extends NetwTestSuite

func before_test() -> void:
	NetwSchemaModel.clear()


func after_test() -> void:
	NetwSchemaModel.clear()


## Verify the builder writes the registry and hands back plain column indices,
## which is what a static initializer can hold.
func test_the_builder_writes_pure_data() -> void:
	var schema := Netw.configure_schema(&"ModelMob")
	var pos := schema.vector3(&"pos")
	var hp := schema.u16(&"hp")

	assert_int(pos).is_equal(0)
	assert_int(hp).is_equal(1)
	assert_str(schema.schema_name).is_equal("ModelMob")

	var declaration := NetwSchemaModel.find(&"ModelMob")
	assert_object(declaration).is_not_null()
	assert_int(declaration.columns.size()).is_equal(2)
	assert_str(declaration.columns[0].key).is_equal("pos")
	assert_int(declaration.columns[0].type).is_equal(
		SchemaCore.ColumnType.VECTOR3,
	)


## Verify a declaration is replicated unless it says otherwise, and that saying
## otherwise keeps the session from minting a table for it.
func test_a_declaration_says_whether_it_wants_a_wire() -> void:
	var replicated := Netw.configure_schema(&"ModelWired")
	replicated.f32(&"a")
	assert_bool(NetwSchemaModel.find(&"ModelWired").replicated).is_true()

	var stored := Netw.configure_schema(&"ModelStored")
	stored.f32(&"a")
	stored.replicated(false)
	assert_bool(NetwSchemaModel.find(&"ModelStored").replicated).is_false()

	var mt := MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)

	assert_bool(mt.api.table_find(&"ModelWired").is_valid()).is_true()
	assert_bool(mt.api.table_find(&"ModelStored").is_valid()).is_false()
	assert_bool(mt.api.schema_find(&"ModelStored").is_valid()).is_true()


## Verify the self-describing tier is declarable through the builder, the tier
## a table refuses and the other two consumers take.
func test_the_builder_declares_a_variant_column() -> void:
	var schema := Netw.configure_schema(&"ModelVariant")
	var blob := schema.variant(&"blob")

	assert_int(blob).is_equal(0)
	assert_int(NetwSchemaModel.find(&"ModelVariant").columns[0].type).is_equal(
		SchemaCore.ColumnType.VARIANT,
	)


## Verify every builder verb names the type it claims, so a column index means
## the same storage on both peers.
func test_every_builder_verb_names_its_type() -> void:
	var schema := Netw.configure_schema(&"ModelTypes")
	var expected := [
		[schema.f32(&"a"), SchemaCore.ColumnType.F32],
		[schema.f64(&"b"), SchemaCore.ColumnType.F64],
		[schema.i8(&"c"), SchemaCore.ColumnType.I8],
		[schema.u8(&"d"), SchemaCore.ColumnType.U8],
		[schema.i16(&"e"), SchemaCore.ColumnType.I16],
		[schema.u16(&"f"), SchemaCore.ColumnType.U16],
		[schema.i32(&"g"), SchemaCore.ColumnType.I32],
		[schema.i64(&"h"), SchemaCore.ColumnType.I64],
		[schema.boolean(&"i"), SchemaCore.ColumnType.BOOL],
		[schema.vector2(&"j"), SchemaCore.ColumnType.VECTOR2],
		[schema.vector3(&"k"), SchemaCore.ColumnType.VECTOR3],
		[schema.vector4(&"l"), SchemaCore.ColumnType.VECTOR4],
		[schema.color(&"m"), SchemaCore.ColumnType.COLOR],
		[schema.quaternion(&"n"), SchemaCore.ColumnType.QUATERNION],
		[schema.entity(&"o"), SchemaCore.ColumnType.ENTITY],
		[schema.variant(&"p"), SchemaCore.ColumnType.VARIANT],
	]

	var declaration := NetwSchemaModel.find(&"ModelTypes")
	for row: Array in expected:
		assert_int(declaration.columns[int(row[0])].type).is_equal(int(row[1]))


## Verify a script reload re-running its initializer extends one declaration
## rather than making a second.
func test_redeclaring_one_name_is_idempotent() -> void:
	var first := Netw.configure_schema(&"ModelReload")
	var pos_a := first.vector3(&"pos")
	var second := Netw.configure_schema(&"ModelReload")
	var pos_b := second.vector3(&"pos")

	assert_int(pos_a).is_equal(pos_b)
	assert_int(NetwSchemaModel.find(&"ModelReload").columns.size()).is_equal(1)
	assert_int(NetwSchemaModel.declarations().size()).is_equal(1)


## Verify a changed shape is refused rather than shifting a column address.
func test_redeclaring_a_column_with_a_new_shape_is_refused() -> void:
	var schema := Netw.configure_schema(&"ModelSkew")
	schema.vector3(&"pos")

	assert_int(schema.vector2(&"pos")).is_equal(-1)
	assert_int(NetwSchemaModel.find(&"ModelSkew").columns.size()).is_equal(1)


## Verify the reliable knob is a declaration axis, since a rare-change table
## has no next commit to heal a lost datagram with.
func test_reliable_rides_the_declaration() -> void:
	var schema := Netw.configure_schema(&"ModelRare")
	schema.u16(&"tier")
	assert_bool(NetwSchemaModel.find(&"ModelRare").reliable).is_false()

	schema.reliable()

	assert_bool(NetwSchemaModel.find(&"ModelRare").reliable).is_true()


## Verify adoption order is name order, because a wire id is the name-sorted
## position and two peers must compute the same one without negotiating.
func test_adoption_order_is_name_order() -> void:
	Netw.configure_schema(&"ModelZulu")
	Netw.configure_schema(&"ModelAlpha")
	Netw.configure_schema(&"ModelMike")

	var names := PackedStringArray()
	for declaration in NetwSchemaModel.declarations():
		names.append(String(declaration.name))

	assert_array(names).is_equal(
		PackedStringArray(["ModelAlpha", "ModelMike", "ModelZulu"]),
	)


## Verify a holder outliving a registry reset can put its schema back, since a
## static initializer runs once and never again.
func test_register_puts_a_declaration_back() -> void:
	var schema := Netw.configure_schema(&"ModelHeld")
	schema.f32(&"a")
	NetwSchemaModel.clear()
	assert_object(NetwSchemaModel.find(&"ModelHeld")).is_null()

	schema.register()

	assert_object(NetwSchemaModel.find(&"ModelHeld")).is_not_null()
	assert_int(NetwSchemaModel.find(&"ModelHeld").columns.size()).is_equal(1)


## Verify a session compiles the registry into its own sealed handles, which is
## the whole point of declaring without one, and that two sessions built from
## one registry mint different handles that agree on the shape.
func test_a_session_adopts_the_registry_at_construction() -> void:
	var declared := Netw.configure_schema(&"ModelAdopted")
	var pos := declared.vector3(
		&"pos",
		NetwQuantizeFixed.new().step(0.03).limits(-512.0, 512.0),
	)
	var hp := declared.u16(&"hp")
	declared.reliable()

	var mt := MultiplayerTree.new()
	mt.name = "AdoptTree"
	add_child(mt)
	auto_free(mt)
	var api := mt.api

	var schema := api.schema_find(&"ModelAdopted")
	assert_bool(schema.is_valid()).is_true()
	assert_int(api.schema_get_column_count(schema)).is_equal(2)
	assert_str(api.schema_get_column_key(schema, pos)).is_equal("pos")
	assert_int(api.schema_get_column_type(schema, hp)).is_equal(
		NetwMultiplayer.ColumnType.COLUMN_U16,
	)

	var table := api.table_find(&"ModelAdopted")
	assert_bool(table.is_valid()).is_true()
	assert_int(api.schema_get_column_count(schema)).is_equal(2)
	assert_int(api.table_get_wire_hash(table)).is_equal(
		api.schema_get_hash(schema),
	)
	assert_object(api._table_core.column_quantizer(table, pos)).is_not_null()
	assert_bool(api._table_core.is_reliable(table)).is_true()

	var other := MultiplayerTree.new()
	other.name = "AdoptTreeTwo"
	add_child(other)
	auto_free(other)
	var other_table := other.api.table_find(&"ModelAdopted")
	assert_that(other_table).is_not_equal(table)
	assert_int(other.api.table_get_wire_hash(other_table)).is_equal(
		api.table_get_wire_hash(table),
	)
