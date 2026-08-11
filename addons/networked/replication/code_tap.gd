## A read-only capture of what every derived set would put on the wire, as the
## canonical rows the ladder and delta-mode geometry are chosen against.
##
## Armed by [code]NETW_CODE_TAP=1[/code] and silent otherwise: disarmed, the tap
## costs one boolean per binding per pump and touches no node. It reads through
## the same public verbs the pump does and writes nothing back, so a captured run
## and an uncaptured run send identical bytes.
##
## One JSON object per line. A set declares itself once, then appends a row per
## tick, so an analyzer reads the declaration for a field's grid and the rows for
## how that field actually moved.
## [codeblock]
## {"decl":"pos:0","record":0,"fields":[{"key":"position","packer":
##   "NetwQuantizeBits","bits":38,"error":0.0156,"min_limit":-512.0,
##   "max_limit":512.0,"bit_count":19}]}
## {"decl":"pos:0","tick":41,"route":7,"values":[[1.02,0.0]]}
## [/codeblock]
## A field's grid is [code]2 * error[/code] wide and starts at its declared
## minimum, so a row's codes are derivable from its declaration and the capture
## stays readable without the engine that wrote it. A field with no
## [member NetwPropertySet.Column.quantizer] declares no grid at all, which is
## what distinguishes an unquantized column from one whose packer has no single
## grid to state.
##
## TODO: delete this file and its pump hook once the wire's delta modes and
## ladder buckets are chosen, since a capture nobody reads is a hook that only
## costs.
extends RefCounted

const _PATH_ENV := "NETW_CODE_TAP_PATH"
const _DEFAULT_PATH := "user://netw_code_tap.jsonl"

static var _armed: int = -1
static var _file: FileAccess = null
static var _opened: bool = false
static var _declared: Dictionary = { }


## Returns whether the tap is capturing this run, reading the environment once.
static func armed() -> bool:
	if _armed < 0:
		_armed = 1 if OS.get_environment("NETW_CODE_TAP") == "1" else 0
	return _armed == 1


## Appends [param binding]'s canonical row under [param tick].
##
## Gathers through [method NetwPropertySetBinding.snapshot_payload] and
## [method NetwPropertySetBinding.canonicalize_payload], so the values recorded
## are the ones the receiver would hold rather than the live ones. A binding
## whose node has freed, or whose set declares nothing, records nothing.
static func record(
		binding: NetwPropertySetBinding,
		route: int,
		tick: int,
) -> void:
	var handle := _handle()
	if handle == null or binding.set == null:
		return
	var canonical := binding.canonicalize_payload(binding.snapshot_payload())
	if canonical.is_empty():
		return
	var key := "%s:%d" % [binding.order_key, binding.set.record]
	_declare(handle, key, binding.set)
	var values: Array = []
	for column: NetwPropertySet.Column in binding.set.columns:
		values.append(_axes(canonical.get(column.key)))
	handle.store_line(
		JSON.stringify(
			{
				"decl": key,
				"tick": tick,
				"route": route,
				"values": values,
			},
		),
	)


## Closes the capture, so a run that ends without tearing the process down
## leaves a complete file.
static func close() -> void:
	if _file:
		_file.close()
		_file = null


# Every quantizer's grid, read off the base class rather than off one subclass.
#
# [NetwQuantize.bit_width] and [NetwQuantize.max_error] answer for every packer,
# including a composite one that has no single grid, so a ladder census can tell
# a column it cannot grid from a column nobody quantized. The limits are read by
# name because only some subclasses carry them, and a subclass that carries
# neither declares its range in its own semantics rather than in a property.
static func _declare_grid(field: Dictionary, column: NetwPropertySet.Column) -> void:
	var quantizer := column.quantizer
	if quantizer == null:
		return
	field["packer"] = quantizer.get_class()
	field["bits"] = quantizer.bit_width(column.type)
	field["error"] = quantizer.max_error(column.type)
	for name: String in ["resolution_step", "min_limit", "max_limit", "bit_count"]:
		var value: Variant = quantizer.get(name)
		if value != null:
			field[name] = value


# A value as the axes its quantizer grids independently, so a ladder measured
# off the capture reads per axis the way the encoder writes it. A type JSON
# cannot carry is recorded as its string form, which is readable but not
# gridded, and no declared column type reaches that branch today.
static func _axes(value: Variant) -> Variant:
	match typeof(value):
		TYPE_VECTOR2:
			return [(value as Vector2).x, (value as Vector2).y]
		TYPE_VECTOR3:
			return [(value as Vector3).x, (value as Vector3).y, (value as Vector3).z]
		TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_NIL:
			return value
	return String(value)


static func _handle() -> FileAccess:
	if not armed():
		return null
	if _file == null:
		var configured := OS.get_environment(_PATH_ENV)
		var path := configured if not configured.is_empty() else _DEFAULT_PATH
		# A reopen appends. Every session teardown closes the tap, so a run with
		# many sessions reopens many times, and opening WRITE each time would
		# truncate away every declaration the earlier sessions wrote.
		if _opened:
			_file = FileAccess.open(path, FileAccess.READ_WRITE)
			if _file:
				_file.seek_end()
		else:
			_file = FileAccess.open(path, FileAccess.WRITE)
		if _file == null:
			# A capture that cannot open its file disarms rather than retrying
			# once per binding per tick for the rest of the run.
			_armed = 0
		else:
			_opened = true
	return _file


static func _declare(
		handle: FileAccess,
		key: String,
		set: NetwPropertySet,
) -> void:
	if _declared.has(key):
		return
	_declared[key] = true
	var fields: Array = []
	for column: NetwPropertySet.Column in set.columns:
		var field: Dictionary = { "key": String(column.key), "lane": column.lane }
		_declare_grid(field, column)
		fields.append(field)
	handle.store_line(
		JSON.stringify({ "decl": key, "record": set.record, "fields": fields }),
	)
