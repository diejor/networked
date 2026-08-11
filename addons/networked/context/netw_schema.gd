## Declares one typed column list beside the code that uses it, in a form a
## [code]static var[/code] initializer can run.
##
## Every method returns a plain [int] column index rather than a handle, because
## a static initializer has no session to mint a handle from and a column index
## is the address every binding takes anyway. The builder writes
## [NetwSchemaModel], each session compiles that registry into its own RIDs, and
## user code fetches the RID once through
## [method NetwMultiplayer.table_find].
## [codeblock]
## class Mobs:
##     static var schema := Netw.configure_schema(&"Mob")
##     static var pos := schema.vector3(
##         &"pos",
##         NetwQuantizeFixed.new().step(0.03),
##     )
##     static var vel := schema.vector3(&"vel")   # unquantized, the memcpy path
##     static var hp  := schema.u16(&"hp")
##
## func _ready() -> void:
##     mobs = Netw.of(self).table_find(Mobs.schema.schema_name)
## [/codeblock]
## A column with no quantizer crosses the wire as a raw little-endian copy of
## its buffer, which is the cheapest path in both directions. A quantizer buys
## bandwidth by paying per element, so reach for one on the columns a link
## actually cares about.
##
## [br][br]One schema serves three consumers, so a declaration that only wants
## the database says so with [method replicated] and mints no table at all.
## [method variant] is the tier a table refuses and the other two accept.
##
## [br][br]These method names are convenience and never freeze. They compile
## into [method NetwMultiplayer.schema_add_column] and
## [method NetwMultiplayer.schema_set_column_quantizer], which do.
class_name NetwSchema
extends RefCounted

## The name this schema is declared under and the key
## [method NetwMultiplayer.schema_find] answers to.
var schema_name: StringName:
	get:
		return _declaration.name if _declaration else &""

var _declaration: NetwSchemaModel.Declaration


func _init(declaration: NetwSchemaModel.Declaration) -> void:
	_declaration = declaration


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_F32] column.
func f32(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_F32, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_F64] column.
func f64(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_F64, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_I8] column.
func i8(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_I8, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_U8] column.
func u8(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_U8, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_I16] column.
func i16(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_I16, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_U16] column.
func u16(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_U16, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_I32] column.
func i32(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_I32, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_I64] column.
func i64(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_I64, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_BOOL] column, one bit per element
## on the wire.
func boolean(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_BOOL, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_VECTOR2] column.
func vector2(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_VECTOR2, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_VECTOR3] column.
func vector3(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_VECTOR3, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_VECTOR4] column.
func vector4(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_VECTOR4, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_COLOR] column.
func color(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_COLOR, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_QUATERNION] column, stored as a
## [PackedVector4Array] and read as [Quaternion] so
## [NetwQuantizeQuaternion] can pack it smallest-three.
func quaternion(
		key: StringName,
		quantizer: NetwQuantize = null,
		stride: int = 1,
) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_QUATERNION, stride, quantizer)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_ENTITY] column of routes, the
## forward-only link one table draws to another.
func entity(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_ENTITY, stride, null)


## Declares a [constant NetwMultiplayer.ColumnType.COLUMN_VARIANT] column, the
## self-describing tier a [String] or a [Dictionary] takes.
##
## A schema holding one cannot become a table, because variable width has no
## memcpy and no rows-per-frame budget. Reach for it on a schema the database
## and the property binding consume, and leave the wire's fixed-width tier to
## the columns that can carry it.
func variant(key: StringName, stride: int = 1) -> int:
	return _column(key, NetwMultiplayer.ColumnType.COLUMN_VARIANT, stride, null)


## Returns the pure-data declaration this builder writes.
##
## It is the currency the three consumers share: a session compiles it into a
## table and into a property set, and [method NetwDatabase.declare_table]
## reads it as data to type its columns.
func declaration() -> NetwSchemaModel.Declaration:
	return _declaration


## Puts this declaration back in [NetwSchemaModel] if it is not listed there,
## and returns itself.
##
## A [code]static var[/code] initializer runs once and never again, so a holder
## that outlives a registry reset would otherwise keep a schema no session can
## find. Call this from the [method Node._ready] of whatever publishes the
## schema, which costs nothing when the declaration is already registered.
func register() -> NetwSchema:
	NetwSchemaModel.adopt(_declaration)
	return self


## Sets whether an adopting session mints a replicated table from this schema.
##
## Declaring a schema costs no wire on its own. Passing [code]false[/code] is
## how a schema that only types database columns says so, and the session skips
## the table, the wire id, and the frame budget entirely.
func replicated(value: bool = true) -> NetwSchema:
	if _declaration:
		_declaration.replicated = value
	return self


## Sends this schema's table commits on the reliable lane.
##
## Reach for it when a table changes rarely, because a rare-change table has no
## next commit to heal a lost datagram with. A table published every tick wants
## the default, where loss costs one row one tick of freshness.
func reliable(value: bool = true) -> NetwSchema:
	if _declaration:
		_declaration.reliable = value
	return self


func _column(
		key: StringName,
		type: int,
		stride: int,
		quantizer: NetwQuantize,
) -> int:
	if _declaration == null:
		return -1
	return _declaration.column(key, type, stride, quantizer)
