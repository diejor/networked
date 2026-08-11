## Node-free sync payload encoder and decoder.
##
## Callers gather values and type metadata at the shell edge. These kernels
## consume only value rows and return bytes or [NetwStagedWrites].
class_name NetwSyncKernel
extends RefCounted

## Encodes one volatile row with the frozen sync-frame codec.
static func encode_volatile(
		ordinal: int,
		flags: int,
		values: Array,
		quantizers: Array,
		types: Array,
		tick: int,
		ack: int,
		mask: int = -1,
) -> PackedByteArray:
	var frame := {
		&"ordinal": ordinal,
		&"flags": flags,
		&"values": values,
		&"quantizers": quantizers,
		&"types": types,
		&"tick": tick,
		&"ack": ack,
	}
	if mask >= 0:
		frame[&"mask"] = mask
	return NetwFrameEnvelope.encode_sync_frame(frame)


## Encodes one redundant window newest first.
static func encode_windowed(
		ordinal: int,
		tick: int,
		samples: Array,
		quantizers: Array,
		types: Array,
) -> PackedByteArray:
	return NetwFrameEnvelope.encode_sync_frame(
		{
			&"ordinal": ordinal,
			&"flags": (
					NetwFrameEnvelope.SYNC_FLAG_STAMPED
					| NetwFrameEnvelope.SYNC_FLAG_WINDOWED
			),
			&"samples": samples,
			&"quantizers": quantizers,
			&"types": types,
			&"tick": tick,
		},
	)


## Decodes one volatile row without applying it to a node.
static func decode_volatile(
		payload: PackedByteArray,
		keys: Array[StringName],
		quantizers: Array,
		types: Array,
		last_row: Dictionary = { },
		fallback_row: Dictionary = { },
) -> NetwStagedWrites:
	var frame := NetwFrameEnvelope.decode_sync_frame(
		payload,
		quantizers,
		types,
	)
	if frame.is_empty():
		return null
	var values: Array = frame.get(&"values", [])
	var selected: Array[StringName] = []
	var row: Dictionary = { }
	var whole := true
	if int(frame.get(&"flags", 0)) \
			& NetwFrameEnvelope.SYNC_FLAG_MASKED:
		var indices: Array = frame.get(&"indices", [])
		if values.size() != indices.size():
			return null
		whole = indices.size() == keys.size()
		row = last_row.duplicate()
		for key: StringName in keys:
			if not row.has(key):
				row[key] = fallback_row.get(key)
		for index: int in indices:
			if index < 0 or index >= keys.size():
				return null
			selected.append(keys[index])
		for index in selected.size():
			row[selected[index]] = values[index]
	else:
		if values.size() != keys.size():
			return null
		selected.assign(keys)
		for index in keys.size():
			row[keys[index]] = values[index]
	var staged := NetwStagedWrites.new()
	staged.ordinal = int(frame.get(&"ordinal", 0))
	staged.tick = int(frame.get(&"tick", -1))
	staged.ack = int(frame.get(&"ack", -1))
	staged.keys = selected
	staged.values = values
	staged.row = row
	staged.whole = whole
	return staged


## Decodes a redundant input window into staged sample rows.
static func decode_windowed(
		payload: PackedByteArray,
		keys: Array[StringName],
		quantizers: Array,
		types: Array,
) -> NetwStagedWrites:
	var frame := NetwFrameEnvelope.decode_sync_frame(
		payload,
		quantizers,
		types,
	)
	var samples: Array = frame.get(&"samples", [])
	if samples.is_empty():
		return null
	var tick := int(frame.get(&"tick", -1))
	var rows: Array = []
	for sample: Array in samples:
		var age := int(sample[0])
		var values: Array = sample[1]
		if values.size() != keys.size():
			continue
		var row: Dictionary = { }
		for index in keys.size():
			row[keys[index]] = values[index]
		rows.append({ &"tick": tick - age, &"payload": row })
	if rows.is_empty():
		return null
	var staged := NetwStagedWrites.new()
	staged.ordinal = int(frame.get(&"ordinal", 0))
	staged.tick = tick
	staged.ack = int(frame.get(&"ack", -1))
	staged.keys.assign(keys)
	staged.row = rows[0][&"payload"]
	for key: StringName in keys:
		staged.values.append(staged.row[key])
	staged.samples = rows
	staged.tape_epoch = int(frame.get(&"tape_epoch", -1))
	staged.entries = frame.get(&"entries", [])
	return staged


## Encodes one retained-lane mask and positional value row.
static func encode_retained(
		ordinal: int,
		mask: int,
		values: Array,
		quantizers: Array,
		types: Array,
) -> PackedByteArray:
	var writer := NetwBitBufferWriter.new()
	NetwCodec.put_varint(writer, ordinal)
	NetwCodec.put_varint(writer, mask)
	NetwScriptModel.write_values(writer, values, quantizers, types)
	return writer.to_bytes()


## Decodes a retained-lane mask into staged property writes.
static func decode_retained(
		payload: PackedByteArray,
		keys: Array[StringName],
		quantizers: Array,
		types: Array,
) -> NetwStagedWrites:
	var reader := NetwBitBufferReader.create(payload)
	var ordinal := NetwCodec.get_safe_varint(reader)
	var mask := NetwCodec.get_safe_varint(reader)
	var selected: Array[StringName] = []
	for index in keys.size():
		if mask & (1 << index):
			selected.append(keys[index])
	var values := NetwScriptModel.read_values(reader, quantizers, types)
	if values.size() != selected.size():
		return null
	var staged := NetwStagedWrites.new()
	staged.ordinal = ordinal
	staged.keys = selected
	staged.values = values
	for index in selected.size():
		staged.row[selected[index]] = values[index]
	return staged
