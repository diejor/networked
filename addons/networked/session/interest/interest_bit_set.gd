## Packed peer-bit operations used by [InterestEngine].
##
## Each word uses bits [code]0[/code] through [code]62[/code]. The sign bit is
## never touched, which keeps every operation inside positive [int] values.
class_name InterestBitSet
extends RefCounted

const BITS_PER_WORD := 63
const WORD_MASK := 0x7FFFFFFFFFFFFFFF


## Returns the number of words needed for [param bit_count] bits.
static func word_count(bit_count: int) -> int:
	if bit_count <= 0:
		return 0
	return ceili(float(bit_count) / float(BITS_PER_WORD))


## Returns an empty row with room for [param bit_count] bits.
static func empty(bit_count: int) -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(word_count(bit_count))
	return out


## Returns a row with every bit below [param bit_count] set.
static func full(bit_count: int) -> PackedInt64Array:
	var out := empty(bit_count)
	for index in out.size():
		out[index] = WORD_MASK
	var remainder := bit_count % BITS_PER_WORD
	if remainder != 0 and not out.is_empty():
		out[out.size() - 1] = (1 << remainder) - 1
	return out


## Returns a copy of [param row] resized to [param words].
static func resized(row: PackedInt64Array, words: int) -> PackedInt64Array:
	var out := row.duplicate()
	out.resize(maxi(words, 0))
	return out


## Returns a copy of [param row] with [param bit] set to [param value].
static func with_bit(
		row: PackedInt64Array,
		bit: int,
		value: bool = true,
) -> PackedInt64Array:
	assert(bit >= 0, "InterestBitSet.with_bit: bit must be non-negative")
	var word := bit / BITS_PER_WORD
	var out := resized(row, maxi(row.size(), word + 1))
	var mask := 1 << (bit % BITS_PER_WORD)
	if value:
		out[word] |= mask
	else:
		out[word] &= WORD_MASK ^ mask
	return out


## Returns whether [param bit] is set in [param row].
static func test(row: PackedInt64Array, bit: int) -> bool:
	if bit < 0:
		return false
	var word := bit / BITS_PER_WORD
	if word >= row.size():
		return false
	return (row[word] & (1 << (bit % BITS_PER_WORD))) != 0


## Returns the union of [param left] and [param right].
static func union(
		left: PackedInt64Array,
		right: PackedInt64Array,
) -> PackedInt64Array:
	var out := empty(maxi(left.size(), right.size()) * BITS_PER_WORD)
	for index in out.size():
		out[index] = _word(left, index) | _word(right, index)
	return out


## Returns the intersection of [param left] and [param right].
static func intersect(
		left: PackedInt64Array,
		right: PackedInt64Array,
) -> PackedInt64Array:
	var out := empty(maxi(left.size(), right.size()) * BITS_PER_WORD)
	for index in out.size():
		out[index] = _word(left, index) & _word(right, index)
	return out


## Returns bits in [param left] that are absent from [param right].
static func subtract(
		left: PackedInt64Array,
		right: PackedInt64Array,
) -> PackedInt64Array:
	var out := empty(maxi(left.size(), right.size()) * BITS_PER_WORD)
	for index in out.size():
		out[index] = _word(left, index) & (WORD_MASK ^ _word(right, index))
	return out


## Returns the exclusive union of [param left] and [param right].
static func symmetric_difference(
		left: PackedInt64Array,
		right: PackedInt64Array,
) -> PackedInt64Array:
	var out := empty(maxi(left.size(), right.size()) * BITS_PER_WORD)
	for index in out.size():
		out[index] = _word(left, index) ^ _word(right, index)
	return out


## Returns whether [param left] and [param right] represent the same bits.
static func equals(
		left: PackedInt64Array,
		right: PackedInt64Array,
) -> bool:
	var words := maxi(left.size(), right.size())
	for index in words:
		if _word(left, index) != _word(right, index):
			return false
	return true


## Returns every set bit in ascending order.
static func bits(row: PackedInt64Array) -> Array[int]:
	var out: Array[int] = []
	for word_index in row.size():
		var word := row[word_index]
		for offset in BITS_PER_WORD:
			if (word & (1 << offset)) != 0:
				out.append(word_index * BITS_PER_WORD + offset)
	return out


## Returns the number of set bits in [param row].
static func popcount(row: PackedInt64Array) -> int:
	var out := 0
	for word in row:
		var remaining: int = word
		while remaining != 0:
			remaining &= remaining - 1
			out += 1
	return out


static func _word(row: PackedInt64Array, index: int) -> int:
	return row[index] if index < row.size() else 0

# TODO: Rows cross to worker threads by value once native fork and join
# exists. Keep every row a bare PackedInt64Array with 63-bit words. Adding an
# Object or per-row wrapper breaks the lane crossing rule.
