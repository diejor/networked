## The session-free registry every [method Netw.configure_schema] declaration is
## recorded in, and the one each live session compiles its own schemas from.
##
## A declaration written in a [code]static var[/code] initializer runs before
## any session exists and has no node to resolve one through, so it cannot call
## a session verb. It writes pure data here instead, and a session adopts the
## whole registry when it is built. That is what makes a schema declarable
## beside the class that uses it rather than in a setup function.
## [codeblock]
## class Mobs:
##     static var schema := Netw.configure_schema(&"Mob")  # writes NetwSchemaModel
##     static var pos    := schema.vector3(&"pos")         # a plain int, no session
##
## var mobs := api.table_find(&"Mob")                      # the session's own RID
## api.table_write_column(mobs, Mobs.pos, my_positions)
## [/codeblock]
## Nothing here references a session, a node, or the tree, so it is safe to
## touch from the editor and from a static initializer. Two embedded sessions
## adopt the same declarations and mint their own RIDs. Nothing is shared but
## the schema, which is exactly why this is the currency the database consumes
## as well ([method NetwDatabase.declare_table]).
##
## [br][br]Declaration is idempotent per name, because a script reload re-runs
## every static initializer. Re-declaring the same column returns the index it
## already has. Re-declaring it with a different shape is a hard error rather
## than a silent index shift, since a column index is a wire address.
class_name NetwSchemaModel
extends RefCounted

## One declared column, the pure-data half of [SchemaColumn].
class ColumnDeclaration extends RefCounted:
	## The caller-facing name, unique within its declaration.
	var key: StringName

	## One of [enum NetwMultiplayer.ColumnType].
	var type: int

	## How many elements one row occupies.
	var stride: int = 1

	## The bit packer, or [code]null[/code] for the raw memcpy path.
	var quantizer: NetwQuantize


	func _init(
			column_key: StringName,
			column_type: int,
			column_stride: int,
			column_quantizer: NetwQuantize,
	) -> void:
		key = column_key
		type = column_type
		stride = column_stride
		quantizer = column_quantizer


## One schema as declared, before any session compiled it.
##
## The column list is the address order every adopting session seals in, so two
## peers built from the same scripts agree on layout without negotiating it.
## This object is what the three consumers share: a session compiles it into a
## table and into a property set, and [NetwDatabase] reads it as data.
class Declaration extends RefCounted:
	## The name [method NetwMultiplayer.schema_find] and
	## [method NetwMultiplayer.table_find] answer to.
	var name: StringName

	## The declared columns in address order.
	var columns: Array[ColumnDeclaration] = []

	## Whether an adopting session mints a replicated table from this schema.
	## A schema declared for the database alone leaves it [code]false[/code] and
	## costs no wire.
	var replicated: bool = true

	## Whether the table's commits ride the reliable lane. Meaningless while
	## [member replicated] is [code]false[/code].
	var reliable: bool = false


	func _init(schema_name: StringName) -> void:
		name = schema_name


	## Appends one column and returns its index, or [code]-1[/code] when a
	## column of that key is already declared with a different shape.
	##
	## Returning the index a key already holds is what makes a script reload
	## idempotent. Refusing a changed shape is what keeps a column index a
	## stable wire address.
	func column(
			key: StringName,
			type: int,
			stride: int,
			quantizer: NetwQuantize,
	) -> int:
		for i in columns.size():
			var existing := columns[i]
			if existing.key != key:
				continue
			if existing.type == type and existing.stride == stride:
				return i
			return -1
		columns.append(ColumnDeclaration.new(key, type, stride, quantizer))
		return columns.size() - 1


static var _declarations: Dictionary[StringName, Declaration] = { }


## Returns the declaration for [param name], creating it on first use.
static func declare(name: StringName) -> Declaration:
	if name.is_empty():
		return null
	var existing: Declaration = _declarations.get(name)
	if existing:
		return existing
	var fresh := Declaration.new(name)
	_declarations[name] = fresh
	return fresh


## Returns the declaration for [param name], or [code]null[/code].
static func find(name: StringName) -> Declaration:
	return _declarations.get(name)


## Re-registers an existing declaration under its own name.
##
## A [code]static var[/code] holding a [NetwSchema] keeps its declaration alive
## even when the registry no longer lists it, and a static initializer runs
## once and never again. This is how such a holder puts its schema back, which
## is what [method NetwSchema.register] does for the caller.
static func adopt(declaration: Declaration) -> void:
	if declaration == null or declaration.name.is_empty():
		return
	if not _declarations.has(declaration.name):
		_declarations[declaration.name] = declaration


## Returns every declaration in name order, which is the order a session adopts
## them in and the order their wire ids fall out of.
##
## Sorted as [String] rather than [StringName], because [StringName] ordering
## is by internal pointer and two peers would disagree about it.
static func declarations() -> Array[Declaration]:
	var names := PackedStringArray()
	for name: StringName in _declarations:
		names.append(String(name))
	names.sort()
	var out: Array[Declaration] = []
	for name in names:
		out.append(_declarations[StringName(name)])
	return out


## Forgets every declaration.
##
## Declarations are process-wide static data that outlives any one session, so
## this exists for a test that needs a clean registry rather than for runtime
## teardown.
static func clear() -> void:
	_declarations.clear()
