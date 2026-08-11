## Immutable-within-a-pump declaration table for sync sets.
##
## The shell captures component order and schema at registration. The sync
## kernels address only [code](route, ordinal)[/code] rows from this table, so
## no pump or decoder derives wire order by walking the scene tree.
class_name NetwSyncModel
extends RefCounted

## Declaration family. Consumed synchronizers precede derived property sets.
enum Kind {
	CONSUMED,
	DERIVED,
}


## One value-only declaration row.
class SetRow:
	extends RefCounted

	var route: int
	var ordinal: int
	var comp: int
	var kind: Kind
	var key: StringName
	var set: RID
	var record: int
	var schema_hash: int


var _rows: Dictionary[int, Array] = { }
var _rows_by_key: Dictionary[int, Dictionary] = { }


## Declares or replaces one row and returns its assigned wire ordinal.
func declare(
		route: int,
		kind: Kind,
		key: StringName,
		comp: int,
		set: RID,
		record: int,
		schema_hash: int,
) -> int:
	if route <= 0 or key.is_empty():
		return -1
	var route_keys: Dictionary = _rows_by_key.get(route, { })
	var composite := _composite_key(kind, key, record)
	var row := route_keys.get(composite) as SetRow
	if row == null:
		row = SetRow.new()
		row.route = route
		row.kind = kind
		row.key = key
		row.record = record
		var rows: Array = _rows.get(route, [])
		rows.append(row)
		_rows[route] = rows
		route_keys[composite] = row
		_rows_by_key[route] = route_keys
	row.comp = comp
	row.set = set
	row.schema_hash = schema_hash
	_reindex(route)
	return row.ordinal


## Removes one declaration row.
func drop(route: int, kind: Kind, key: StringName, record: int = 0) -> void:
	var route_keys: Dictionary = _rows_by_key.get(route, { })
	var composite := _composite_key(kind, key, record)
	var row := route_keys.get(composite) as SetRow
	if row == null:
		return
	var rows: Array = _rows.get(route, [])
	rows.erase(row)
	route_keys.erase(composite)
	if rows.is_empty():
		_rows.erase(route)
		_rows_by_key.erase(route)
	else:
		_rows[route] = rows
		_rows_by_key[route] = route_keys
		_reindex(route)


## Returns the row at [param ordinal], or [code]null[/code].
func row(route: int, ordinal: int) -> SetRow:
	var rows: Array = _rows.get(route, [])
	if ordinal < 0 or ordinal >= rows.size():
		return null
	return rows[ordinal] as SetRow


## Returns one declaration row by its stable registration key.
func row_for(
		route: int,
		kind: Kind,
		key: StringName,
		record: int = 0,
) -> SetRow:
	var route_keys: Dictionary = _rows_by_key.get(route, { })
	return route_keys.get(_composite_key(kind, key, record)) as SetRow


## Returns the number of consumed rows on [param route].
func consumed_count(route: int) -> int:
	var count := 0
	for row: SetRow in _rows.get(route, []):
		if row.kind == Kind.CONSUMED:
			count += 1
	return count


## Returns a stable copy of [param route]'s declaration rows.
func route_rows(route: int) -> Array[SetRow]:
	var out: Array[SetRow] = []
	for row: SetRow in _rows.get(route, []):
		out.append(row)
	return out


## Removes every declaration for [param route].
func clear_route(route: int) -> void:
	_rows.erase(route)
	_rows_by_key.erase(route)


## Removes every declaration row.
func clear() -> void:
	_rows.clear()
	_rows_by_key.clear()


# Assigns ordinals once after a declaration mutation, never during a pump.
func _reindex(route: int) -> void:
	var rows: Array = _rows.get(route, [])
	rows.sort_custom(
		func(a: SetRow, b: SetRow) -> bool:
			if a.kind != b.kind:
				return a.kind < b.kind
			if a.key != b.key:
				return String(a.key) < String(b.key)
			return a.record < b.record,
	)
	for ordinal in rows.size():
		(rows[ordinal] as SetRow).ordinal = ordinal


# Builds a collision-free key inside one route table.
static func _composite_key(
		kind: Kind,
		key: StringName,
		record: int,
) -> StringName:
	return StringName("%d:%s:%d" % [kind, key, record])
