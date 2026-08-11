## Pure codec for typed session join arguments.
class_name NetwJoinCodec
extends RefCounted

## Packs [member JoinPayload.arg_values] for [param handler].
static func encode(
		payload: JoinPayload,
		handler: Callable,
		quantizers: Array = [],
) -> void:
	if payload.arg_values.is_empty() or not handler.is_valid():
		payload.arg_bytes = PackedByteArray()
		payload.schema_hash = 0
		return
	var writer := NetwBitBufferWriter.new()
	NetwScriptModel.write_values(
		writer,
		payload.arg_values,
		quantizers,
		_arg_types(handler),
	)
	payload.arg_bytes = writer.to_bytes()
	payload.schema_hash = _schema_hash(handler, quantizers)


## Decodes [member JoinPayload.arg_bytes] for [param handler].
static func decode(
		payload: JoinPayload,
		handler: Callable,
		quantizers: Array = [],
) -> bool:
	if payload.arg_bytes.is_empty():
		payload.arg_values = []
		return true
	if not handler.is_valid():
		return false
	if payload.schema_hash != _schema_hash(handler, quantizers):
		return false
	var reader := NetwBitBufferReader.create(payload.arg_bytes)
	payload.arg_values = NetwScriptModel.read_call_args(
		reader,
		quantizers,
		_arg_types(handler),
	)
	return true


# Returns the application argument types after the framework join record.
static func _arg_types(handler: Callable) -> Array:
	var object := handler.get_object()
	var script: Script = object.get_script() if object else null
	var types := NetwScriptModel.get_method_arg_types(
		script,
		handler.get_method(),
	)
	return types.slice(1) if not types.is_empty() else []


# Hashes argument types and quantizer classes into the wire schema id.
static func _schema_hash(handler: Callable, quantizers: Array) -> int:
	var signature := ""
	for type: int in _arg_types(handler):
		signature += str(type) + ","
	signature += "|"
	for quantizer: NetwQuantize in quantizers:
		signature += (
				quantizer.get_class() if quantizer != null else "_"
		) + ","
	return signature.hash()
