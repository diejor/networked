@tool
## Compact byte codec for tick-stamped state and input payloads.
##
## The wire never carries property names: both peers derive the ordered key list
## and the per-key [NetwQuantize] from the same synchronizer config. A configured
## property is bit-packed by its quantizer with no type tag, an unconfigured
## property byte-aligns and writes a one-byte type tag plus a raw value
## ([code]bool[/code]/[code]int[/code]/[Vector2] packed, else
## [method @GlobalScope.var_to_bytes]). This is the value-encoding core shared by
## [InputSynchronizer]'s window and [StateSynchronizer]'s bundled snapshot.
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
## [param quantizers] is a list parallel to [param keys] (null entries fall back to
## raw). [param types] is the parallel [enum Variant.Type] list a decoder needs to
## reconstruct a quantized value, derived from the live property type.
class_name NetwCodec

## Raw type tags for the unconfigured (no-quantizer) path. Stable: a decoder reads
## whatever an encoder wrote, so tags may be appended but never renumbered.
enum {
	T_FALLBACK = 0,
	T_BOOL = 1,
	T_INT = 2,
	T_VECTOR2 = 3,
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
	w.put_aligned_u32(tick)
	var spb := StreamPeerBuffer.new()
	spb.put_32(ack)
	w.put_aligned_bytes(spb.data_array)
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
	var tick := r.get_aligned_u32()
	var spb := StreamPeerBuffer.new()
	spb.data_array = r.get_aligned_bytes(4)
	var ack := spb.get_32()
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
	w.put_aligned_u32(base_tick)
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
	var base_tick := r.get_aligned_u32()
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


static func _encode_value(
		w: NetwBitBuffer.Writer,
		value: Variant,
		quantizer: NetwQuantize,
) -> void:
	if quantizer:
		quantizer.write(w, value)
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
		return quantizer.read(r, type as Variant.Type)
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
		_:
			return T_FALLBACK
