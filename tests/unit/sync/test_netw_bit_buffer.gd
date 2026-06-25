## Unit tests for [NetwBitBuffer] writer/reader symmetry.
class_name TestNetwBitBuffer
extends NetwTestSuite

func test_bit_roundtrip_arbitrary_widths() -> void:
	var w := NetwBitBuffer.Writer.new()
	w.put_bits(5, 3)
	w.put_bits(300, 9)
	w.put_bits(1, 1)
	w.put_bits(0, 4)

	var r := NetwBitBuffer.Reader.new(w.to_bytes())
	assert_int(r.get_bits(3)).is_equal(5)
	assert_int(r.get_bits(9)).is_equal(300)
	assert_int(r.get_bits(1)).is_equal(1)
	assert_int(r.get_bits(4)).is_equal(0)


func test_partial_final_byte_flushes() -> void:
	var w := NetwBitBuffer.Writer.new()
	w.put_bits(0b101, 3)
	var bytes := w.to_bytes()
	assert_int(bytes.size()).is_equal(1)
	assert_int(bytes[0]).is_equal(0b101)

	var r := NetwBitBuffer.Reader.new(bytes)
	assert_int(r.get_bits(3)).is_equal(5)


func test_aligned_helpers_roundtrip() -> void:
	var w := NetwBitBuffer.Writer.new()
	w.put_aligned_u8(200)
	w.put_aligned_u32(0xDEADBEEF)
	w.put_aligned_bytes(PackedByteArray([1, 2, 3, 4, 5]))

	var r := NetwBitBuffer.Reader.new(w.to_bytes())
	assert_int(r.get_aligned_u8()).is_equal(200)
	assert_int(r.get_aligned_u32()).is_equal(0xDEADBEEF)
	assert_that(r.get_aligned_bytes(5)).is_equal(PackedByteArray([1, 2, 3, 4, 5]))


func test_bits_then_align_skips_padding() -> void:
	var w := NetwBitBuffer.Writer.new()
	w.put_bits(7, 3)
	w.put_aligned_u8(123)
	w.put_bits(2, 2)
	w.put_aligned_u32(99)

	var r := NetwBitBuffer.Reader.new(w.to_bytes())
	assert_int(r.get_bits(3)).is_equal(7)
	assert_int(r.get_aligned_u8()).is_equal(123)
	assert_int(r.get_bits(2)).is_equal(2)
	assert_int(r.get_aligned_u32()).is_equal(99)
