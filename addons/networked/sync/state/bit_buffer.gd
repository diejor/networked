@tool
## Bit-granular byte buffer used by [NetwCodec] to pack quantized values without
## byte rounding.
##
## A quantized field occupies exactly its declared width. The writer accumulates
## bits LSB-first and flushes whole bytes; the reader pulls whole bytes and serves
## bits back in the same order. Byte-aligned helpers exist for framing headers and
## the raw [method @GlobalScope.var_to_bytes] fallback, so a packet mixes a
## byte-aligned header, a bit-packed payload, and an aligned tail coherently.
##
## [codeblock]
## var w := NetwBitBuffer.Writer.new()
## w.put_aligned_u32(tick)      # header, byte-aligned
## w.put_bits(quantized_x, 11)  # payload, bit-packed
## w.put_bits(quantized_y, 11)
## var bytes := w.to_bytes()    # flushes the partial final byte
##
## var r := NetwBitBuffer.Reader.new(bytes)
## var tick := r.get_aligned_u32()
## var x := r.get_bits(11)
## var y := r.get_bits(11)
## [/codeblock]
##
## Widths come from the shared synchronizer config on both peers, never the wire,
## so a one-bit offset cannot drift between encoder and decoder.
class_name NetwBitBuffer


## Accumulates bits and bytes into a [PackedByteArray].
class Writer:
	var _out: PackedByteArray = PackedByteArray()
	var _acc: int = 0
	var _nbits: int = 0

	## Writes the low [param count] bits of [param value], LSB-first.
	func put_bits(value: int, count: int) -> void:
		if count <= 0:
			return
		var mask := (1 << count) - 1
		_acc |= (value & mask) << _nbits
		_nbits += count
		while _nbits >= 8:
			_out.append(_acc & 0xFF)
			_acc >>= 8
			_nbits -= 8

	## Flushes any partial byte (zero-padded) so the next write is byte-aligned.
	func align() -> void:
		if _nbits > 0:
			_out.append(_acc & 0xFF)
			_acc = 0
			_nbits = 0

	## Aligns, then writes one byte.
	func put_aligned_u8(value: int) -> void:
		align()
		_out.append(value & 0xFF)

	## Aligns, then writes a 32-bit little-endian value.
	func put_aligned_u32(value: int) -> void:
		align()
		_out.append(value & 0xFF)
		_out.append((value >> 8) & 0xFF)
		_out.append((value >> 16) & 0xFF)
		_out.append((value >> 24) & 0xFF)

	## Aligns, then appends [param bytes] verbatim.
	func put_aligned_bytes(bytes: PackedByteArray) -> void:
		align()
		_out.append_array(bytes)

	## Flushes the partial final byte and returns the buffer.
	func to_bytes() -> PackedByteArray:
		align()
		return _out


## Serves bits and bytes back from a [PackedByteArray] in the order [Writer] wrote
## them.
class Reader:
	var _in: PackedByteArray
	var _pos: int = 0
	var _acc: int = 0
	var _nbits: int = 0

	func _init(bytes: PackedByteArray) -> void:
		_in = bytes

	## Reads [param count] bits, LSB-first.
	func get_bits(count: int) -> int:
		if count <= 0:
			return 0
		while _nbits < count:
			var b := _in[_pos] if _pos < _in.size() else 0
			_pos += 1
			_acc |= b << _nbits
			_nbits += 8
		var mask := (1 << count) - 1
		var result := _acc & mask
		_acc >>= count
		_nbits -= count
		return result

	## Discards the leftover bits of the current byte so the next read is
	## byte-aligned. The leftover is always under 8 bits, so the byte cursor is
	## already at the next boundary.
	func align() -> void:
		_acc = 0
		_nbits = 0

	## Aligns, then reads one byte.
	func get_aligned_u8() -> int:
		align()
		var v := _in[_pos] if _pos < _in.size() else 0
		_pos += 1
		return v

	## Aligns, then reads a 32-bit little-endian value.
	func get_aligned_u32() -> int:
		align()
		var v := 0
		for i in 4:
			var b := _in[_pos] if _pos < _in.size() else 0
			_pos += 1
			v |= b << (i * 8)
		return v

	## Aligns, then reads [param count] raw bytes.
	func get_aligned_bytes(count: int) -> PackedByteArray:
		align()
		var slice := _in.slice(_pos, _pos + count)
		_pos += count
		return slice
