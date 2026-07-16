@tool
## Compact byte codec for tick-stamped state and input payloads.
##
## The wire never carries property names: both peers derive the ordered key list
## and the per-key [NetwQuantize] from the same synchronizer config. A configured
## property is bit-packed by its quantizer with no type tag, an unconfigured
## property byte-aligns and writes a one-byte type tag plus a raw value
## ([code]bool[/code]/[code]int[/code]/[Vector2] packed, else
## [method @GlobalScope.var_to_bytes]). This is the value-encoding core shared by
## the windowed input lane and the state set's bundled snapshot.
##
## [codeblock]
## # snapshot (state blob): tick + ack header, then the bit-packed payload
## var bytes := NetwCodec.encode_snapshot(tick, ack, payload, keys, quantizers)
## var frame := NetwCodec.decode_snapshot(bytes, keys, quantizers, types)
## #   frame == { tick: int, ack: int, payload: { key: value } }
##
## # window (input redundancy): base_tick + count header, then per-sample payloads
## var w := NetwCodec.encode_window(samples, keys, quantizers)
## var s := NetwCodec.decode_window(w, keys, quantizers, types)
## [/codeblock]
##
## [code]quantizers[/code] is a list parallel to [code]keys[/code] (null entries fall back to
## raw). [code]types[/code] is the parallel [enum Variant.Type] list a decoder needs to
## reconstruct a quantized value, derived from the live property type.
class_name NetwCodec

## Raw type tags for the unconfigured (no-quantizer) path. Stable: a decoder reads
## whatever an encoder wrote, so tags may be appended but never renumbered.
enum {
	T_FALLBACK = 0,
	T_BOOL = 1,
	T_INT = 2,
	T_VECTOR2 = 3,
	T_FLOAT = 4,
	T_VECTOR3 = 5,
}


## Encodes [param tick], [param ack], and [param payload] into a snapshot blob.
static func encode_snapshot(
		tick: int,
		ack: int,
		payload: Dictionary,
		keys: Array[StringName],
		quantizers: Array,
) -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	_put_svarint(w, tick)
	_put_svarint(w, ack)
	encode_payload(w, payload, keys, quantizers)
	return w.to_bytes()


## Decodes a snapshot blob into [code]{ tick, ack, payload }[/code]. Returns an
## empty [Dictionary] for empty [param bytes].
static func decode_snapshot(
		bytes: PackedByteArray,
		keys: Array[StringName],
		quantizers: Array,
		types: Array,
) -> Dictionary:
	if bytes.is_empty():
		return { }
	var r := NetwBitBuffer.Reader.new(bytes)
	var tick_val := _get_safe_varint(r)
	if tick_val < 0:
		return { }
	var ack_val := _get_safe_varint(r)
	if ack_val < 0:
		return { }
	var tick := _decode_zigzag(tick_val)
	var ack := _decode_zigzag(ack_val)
	var payload := decode_payload(r, keys, quantizers, types)
	return { &"tick": tick, &"ack": ack, &"payload": payload }


## Encodes a tick-ascending window of [code]{ tick, input }[/code] samples. Returns
## an empty array for an empty window.
static func encode_window(
		samples: Array[Dictionary],
		keys: Array[StringName],
		quantizers: Array,
) -> PackedByteArray:
	if samples.is_empty():
		return PackedByteArray()
	var w := NetwBitBuffer.Writer.new()
	var base_tick := int(samples[samples.size() - 1].get(&"tick", 0))
	_put_varint(w, base_tick + 1)
	w.put_aligned_u8(samples.size())
	for sample: Dictionary in samples:
		var tick := int(sample.get(&"tick", 0))
		w.put_aligned_u8(base_tick - tick)
		encode_payload(w, sample.get(&"input", { }), keys, quantizers)
	return w.to_bytes()


## Decodes a window blob back into the sample shape. Returns an empty array for
## empty [param bytes].
static func decode_window(
		bytes: PackedByteArray,
		keys: Array[StringName],
		quantizers: Array,
		types: Array,
) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if bytes.is_empty():
		return out
	var r := NetwBitBuffer.Reader.new(bytes)
	var base_tick_val := _get_safe_varint(r)
	if base_tick_val < 0:
		return out
	var base_tick := base_tick_val - 1
	var count := r.get_aligned_u8()
	for s in count:
		var tick := base_tick - r.get_aligned_u8()
		var payload := decode_payload(r, keys, quantizers, types)
		out.append({ &"tick": tick, &"input": payload })
	return out


## Writes [param payload] values into [param w] in [param keys] order.
static func encode_payload(
		w: NetwBitBuffer.Writer,
		payload: Dictionary,
		keys: Array[StringName],
		quantizers: Array,
) -> void:
	for i in keys.size():
		var q: NetwQuantize = quantizers[i] if i < quantizers.size() else null
		_encode_value(w, payload.get(keys[i]), q)


## Reads a [code]{ key: value }[/code] payload from [param r] in [param keys] order.
static func decode_payload(
		r: NetwBitBuffer.Reader,
		keys: Array[StringName],
		quantizers: Array,
		types: Array,
) -> Dictionary:
	var out: Dictionary = { }
	for i in keys.size():
		var q: NetwQuantize = quantizers[i] if i < quantizers.size() else null
		var t: int = types[i] if i < types.size() else TYPE_NIL
		out[keys[i]] = _decode_value(r, t, q)
	return out


## Writes a single [param value] into [param w], bit-packed by
## [param quantizer] when set, otherwise byte-aligned with a type tag. The
## public entry to the value-encoding core, shared by entity RPC arguments.
static func encode_value(
		w: NetwBitBuffer.Writer,
		value: Variant,
		quantizer: NetwQuantize,
) -> void:
	_encode_value(w, value, quantizer)


## Reads a single value written by [method encode_value]. [param type] is the
## reconstructed [enum Variant.Type] a quantized value needs, ignored on the
## tagged path.
static func decode_value(
		r: NetwBitBuffer.Reader,
		type: int,
		quantizer: NetwQuantize,
) -> Variant:
	return _decode_value(r, type, quantizer)


static func _encode_value(
		w: NetwBitBuffer.Writer,
		value: Variant,
		quantizer: NetwQuantize,
) -> void:
	if quantizer:
		quantizer._write(w, value)
		return
	var t := _type_byte(value)
	w.put_aligned_u8(t)
	match t:
		T_BOOL:
			w.put_aligned_u8(1 if value else 0)
		T_INT:
			var spb := StreamPeerBuffer.new()
			spb.put_64(int(value))
			w.put_aligned_bytes(spb.data_array)
		T_VECTOR2:
			var spb := StreamPeerBuffer.new()
			spb.put_float(value.x)
			spb.put_float(value.y)
			w.put_aligned_bytes(spb.data_array)
		T_FLOAT:
			var spb := StreamPeerBuffer.new()
			spb.put_float(float(value))
			w.put_aligned_bytes(spb.data_array)
		T_VECTOR3:
			var spb := StreamPeerBuffer.new()
			spb.put_float(value.x)
			spb.put_float(value.y)
			spb.put_float(value.z)
			w.put_aligned_bytes(spb.data_array)
		_:
			var packed := var_to_bytes(value)
			w.put_aligned_u32(packed.size())
			w.put_aligned_bytes(packed)


static func _decode_value(
		r: NetwBitBuffer.Reader,
		type: int,
		quantizer: NetwQuantize,
) -> Variant:
	if quantizer:
		return quantizer._read(r, type as Variant.Type)
	var t := r.get_aligned_u8()
	match t:
		T_BOOL:
			return r.get_aligned_u8() != 0
		T_INT:
			var spb := StreamPeerBuffer.new()
			spb.data_array = r.get_aligned_bytes(8)
			return spb.get_64()
		T_VECTOR2:
			var spb := StreamPeerBuffer.new()
			spb.data_array = r.get_aligned_bytes(8)
			var x := spb.get_float()
			var y := spb.get_float()
			return Vector2(x, y)
		T_FLOAT:
			var spb := StreamPeerBuffer.new()
			spb.data_array = r.get_aligned_bytes(4)
			return spb.get_float()
		T_VECTOR3:
			var spb := StreamPeerBuffer.new()
			spb.data_array = r.get_aligned_bytes(12)
			var x := spb.get_float()
			var y := spb.get_float()
			var z := spb.get_float()
			return Vector3(x, y, z)
		_:
			var size := r.get_aligned_u32()
			return bytes_to_var(r.get_aligned_bytes(size))


static func _type_byte(value: Variant) -> int:
	match typeof(value):
		TYPE_BOOL:
			return T_BOOL
		TYPE_INT:
			return T_INT
		TYPE_VECTOR2:
			return T_VECTOR2
		TYPE_FLOAT:
			return T_FLOAT
		TYPE_VECTOR3:
			return T_VECTOR3
		_:
			return T_FALLBACK


## Writes [param value] as a varint into [param w].
static func put_varint(w: NetwBitBuffer.Writer, value: int) -> void:
	_put_varint(w, value)


## Reads a varint from [param r], returning [code]-1[/code] on error/overflow.
static func get_safe_varint(r: NetwBitBuffer.Reader) -> int:
	return _get_safe_varint(r)


static func _put_varint(w: NetwBitBuffer.Writer, value: int) -> void:
	for __ in 5:
		var byte := value & 0x7F
		value >>= 7
		if value > 0:
			w.put_aligned_u8(byte | 0x80)
		else:
			w.put_aligned_u8(byte)
			break


static func _put_svarint(w: NetwBitBuffer.Writer, value: int) -> void:
	_put_varint(w, _encode_zigzag(value))


static func _encode_zigzag(value: int) -> int:
	return (value << 1) if value >= 0 else ((-value << 1) - 1)


static func _decode_zigzag(value: int) -> int:
	return (value >> 1) if value & 1 == 0 else -((value >> 1) + 1)


static func _get_safe_varint(r: NetwBitBuffer.Reader) -> int:
	var value := 0
	for i in 5:
		var byte := r.get_aligned_u8()
		value |= (byte & 0x7F) << (i * 7)
		if (byte & 0x80) == 0:
			return value
	Netw.dbg.error("NetwCodec: Varint overflow/corrupt packet.")
	return -1
