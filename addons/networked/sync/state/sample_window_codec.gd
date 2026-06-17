## Compact byte codec for a window of tick-keyed payload samples.
##
## The wire never carries property names. Both peers derive the ordered key list
## from the same registered config, so the encoder writes only values and the
## decoder zips them back onto the keys. A single type-byte layout is written once
## per window, then each value is raw-packed by type, falling back to
## [method @GlobalScope.var_to_bytes] for any type outside the raw set. This is
## the value-encoding core that [InputSynchronizer] uses for
## [constant InputSynchronizer.INPUT_WINDOW] today, and the Phase T diff channel
## reuses later.
##
## [codeblock]
## # samples shape matches NetwTimeline.inputs_in_range:
## #   [{ tick: int, input: { key: value } }, ...]   tick-ascending
## var keys := input.snapshot_payload().keys()        # [&"motion", &"bombing"]
## var bytes := SampleWindowCodec.encode(samples, keys)
## var same  := SampleWindowCodec.decode(bytes, keys) # round-trips exactly
##
## # layout, base_tick = newest sample tick:
## #   base_tick u32 │ count u8 │ prop_count u8, type-byte ×prop_count
## #   per sample: tick_offset u8 (base_tick - tick), then values in key order
## [/codeblock]
class_name SampleWindowCodec

## Type tags for the raw-packed set. [constant T_FALLBACK] marks a value encoded
## through [method @GlobalScope.var_to_bytes]. Values are stable: a decoder reads
## whatever an encoder wrote, so new tags may be appended but never renumbered.
enum {
	T_FALLBACK = 0,
	T_BOOL = 1,
	T_INT = 2,
	T_VECTOR2 = 3,
}


## Encodes [param samples] into a [PackedByteArray], reading values by
## [param keys] in order.
##
## [param samples] is tick-ascending, each entry
## [code]{ &"tick": int, &"input": Dictionary }[/code]. Returns an empty array for
## an empty window. The type layout is taken from the first sample's payload, so
## every sample must share the same value type per key (true for a fixed config).
static func encode(samples: Array[Dictionary], keys: Array[StringName]) -> PackedByteArray:
	if samples.is_empty():
		return PackedByteArray()

	var buf := StreamPeerBuffer.new()
	var base_tick := int(samples[samples.size() - 1].get(&"tick", 0))
	buf.put_u32(base_tick)
	buf.put_u8(samples.size())

	var first_payload: Dictionary = samples[0].get(&"input", { })
	var types: Array[int] = []
	buf.put_u8(keys.size())
	for key: StringName in keys:
		var t := _type_byte(first_payload.get(key))
		types.append(t)
		buf.put_u8(t)

	for sample: Dictionary in samples:
		var tick := int(sample.get(&"tick", 0))
		buf.put_u8(base_tick - tick)
		var payload: Dictionary = sample.get(&"input", { })
		for i in keys.size():
			_encode_value(types[i], payload.get(keys[i]), buf)

	return buf.data_array


## Decodes [param bytes] produced by [method encode] back into the sample shape,
## mapping each value onto [param keys] by position. Returns an empty array for
## empty [param bytes].
static func decode(bytes: PackedByteArray, keys: Array[StringName]) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if bytes.is_empty():
		return out

	var buf := StreamPeerBuffer.new()
	buf.data_array = bytes
	var base_tick := buf.get_u32()
	var count := buf.get_u8()
	var prop_count := buf.get_u8()

	var types: Array[int] = []
	for i in prop_count:
		types.append(buf.get_u8())

	for s in count:
		var tick := base_tick - buf.get_u8()
		var payload: Dictionary = { }
		for i in prop_count:
			var value: Variant = _decode_value(types[i], buf)
			if i < keys.size():
				payload[keys[i]] = value
		out.append({ &"tick": tick, &"input": payload })

	return out


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


static func _encode_value(type_byte: int, value: Variant, buf: StreamPeerBuffer) -> void:
	match type_byte:
		T_BOOL:
			buf.put_u8(1 if value else 0)
		T_INT:
			buf.put_64(int(value))
		T_VECTOR2:
			buf.put_float(value.x)
			buf.put_float(value.y)
		_:
			var packed := var_to_bytes(value)
			buf.put_u32(packed.size())
			buf.put_data(packed)


static func _decode_value(type_byte: int, buf: StreamPeerBuffer) -> Variant:
	match type_byte:
		T_BOOL:
			return buf.get_u8() != 0
		T_INT:
			return buf.get_64()
		T_VECTOR2:
			var x := buf.get_float()
			var y := buf.get_float()
			return Vector2(x, y)
		_:
			var size := buf.get_u32()
			return bytes_to_var(buf.get_data(size)[1])
