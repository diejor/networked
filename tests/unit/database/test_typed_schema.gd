## Laws for a database that speaks the schema.
##
## A table declared from a [NetwSchemaModel.Declaration] carries its column
## types, which is what lets a load reject a value of the wrong shape instead of
## assigning it. A table declared from bare names carries none and coerces
## nothing, so the untyped form cannot silently change a game's saves.
class_name TestTypedSchema
extends NetwTestSuite

func _make_db() -> NetwDatabase:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	db.backend = backend
	return db


func _rocks() -> NetwSchemaModel.Declaration:
	var declaration := NetwSchemaModel.Declaration.new(&"rocks")
	declaration.replicated = false
	declaration.column(&"health", SchemaCore.ColumnType.F64, 1, null)
	declaration.column(&"origin", SchemaCore.ColumnType.VECTOR2, 1, null)
	return declaration


## Verify the declaration is what carries the types, so the one column list the
## table and the property binding compile from types the database too.
func test_a_declaration_types_the_columns() -> void:
	var db := _make_db()

	db.declare_table(&"rocks", _rocks())

	assert_array(db.get_registered_columns(&"rocks")).is_equal(
		[&"health", &"origin"] as Array[StringName],
	)
	assert_int(db.get_column_type(&"rocks", &"health")).is_equal(
		SchemaCore.ColumnType.F64,
	)
	assert_int(db.get_column_type(&"rocks", &"origin")).is_equal(
		SchemaCore.ColumnType.VECTOR2,
	)
	assert_int(db.get_column_type(&"rocks", &"never")).is_equal(-1)


## Verify the bare-names form still declares the table and types nothing, which
## is what keeps it from changing a save it was never told the shape of.
func test_the_bare_names_form_types_nothing() -> void:
	var db := _make_db()

	db.declare_table(&"rocks", [&"health", &"origin"])

	assert_array(db.get_registered_columns(&"rocks")).is_equal(
		[&"health", &"origin"] as Array[StringName],
	)
	assert_int(db.get_column_type(&"rocks", &"health")).is_equal(-1)


## Verify a stored value of the wrong shape is dropped and named rather than
## assigned, so the live scene keeps its default for that one column and the
## rest of the row still loads.
func test_a_mistyped_value_is_rejected_to_the_default() -> void:
	var db := _make_db()
	db.declare_table(&"rocks", _rocks())

	var kept := db._reject_mistyped(
		&"rocks",
		&"r1",
		{ &"health": 50.0, &"origin": Vector2.ONE },
	)
	assert_that(kept).is_equal({ &"health": 50.0, &"origin": Vector2.ONE })

	var rejected := db._reject_mistyped(
		&"rocks",
		&"r1",
		{ &"health": 50.0, &"origin": "not a vector" },
	)
	assert_bool(rejected.has(&"health")).is_true()
	assert_bool(rejected.has(&"origin")).is_false()


## Verify an untyped table rejects nothing, since it was never told a shape to
## judge against.
func test_an_untyped_table_rejects_nothing() -> void:
	var db := _make_db()
	db.declare_table(&"rocks", [&"health", &"origin"])

	var kept := db._reject_mistyped(
		&"rocks",
		&"r1",
		{ &"health": "fifty", &"origin": 3 },
	)

	assert_that(kept).is_equal({ &"health": "fifty", &"origin": 3 })


## Verify a stray column no longer purges the record by default, because a save
## file is the player's and a schema that grew a column is the ordinary case.
func test_a_stray_column_no_longer_purges() -> void:
	var db := _make_db()
	assert_int(db.mismatch_policy).is_equal(
		NetwDatabase.SchemaMismatchPolicy.LOAD_PARTIAL,
	)
	db.declare_table(&"rocks", _rocks())
	var backend := db.backend as TestMemoryBackend

	var record := { &"health": 50.0, &"origin": Vector2.ONE, &"gold": 5 }
	var diff := db._diff_record(&"rocks", &"r1", record)
	var out := [OK]
	var result := db._apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_int(out[0]).is_equal(OK)
	assert_bool(result.has(&"health")).is_true()
	assert_bool(result.has(&"gold")).is_false()
	assert_bool(backend.delete_calls.is_empty()).is_true()


## Verify the self-describing tier is exempt: a [constant
## SchemaCore.ColumnType.VARIANT] column holds whatever it holds, so judging its
## shape would reject every legal value.
func test_a_variant_column_is_never_rejected() -> void:
	var db := _make_db()
	var declaration := NetwSchemaModel.Declaration.new(&"blobs")
	declaration.replicated = false
	declaration.column(&"payload", SchemaCore.ColumnType.VARIANT, 1, null)
	db.declare_table(&"blobs", declaration)

	var kept := db._reject_mistyped(
		&"blobs",
		&"b1",
		{ &"payload": { &"any": [1, 2, 3] } },
	)

	assert_bool(kept.has(&"payload")).is_true()
