## Laws for what a reader does when it runs out of bits.
##
## Running out is a failure and never a value. A reader that serves zeros past
## the end of its buffer turns a truncated frame into a plausible one, and SPAWN
## is where that matters most: [code]route[/code] is the first field, so it is
## the one field an inserted varint does not shift, and it was the only field
## guarded. Every field after a shortfall is a zero the reader invented.
##
## The refusal is frame-level on purpose. A truncated frame is dropped and
## counted; whether a peer that keeps sending them should lose its connection is
## a policy question this suite does not answer.
class_name TestSpawnFrameRefusal
extends NetwTestSuite


## Verify a reader reports the shortfall rather than only the zeros, which is
## the whole mechanism a decoder can refuse on.
func test_a_reader_that_runs_out_says_so() -> void:
	var writer := NetwBitBufferWriter.new()
	writer.put_aligned_u32(4242)
	var reader := NetwBitBufferReader.create(writer.to_bytes())

	assert_bool(reader.ok()).is_true()
	assert_int(reader.get_aligned_u32()).is_equal(4242)
	assert_bool(reader.ok()).is_true()
	var invented := reader.get_aligned_u32()

	assert_int(invented).is_equal(0)
	assert_bool(reader.ok()).is_false()


## Verify the zeros still come, so a caller that never asks reads exactly what
## it read before. The flag is an addition, not a behaviour change.
func test_the_zeros_still_come_for_a_caller_that_never_asks() -> void:
	var reader := NetwBitBufferReader.create(PackedByteArray())

	assert_int(reader.get_bits(8)).is_equal(0)
	assert_int(reader.get_aligned_u16()).is_equal(0)
	assert_bool(reader.ok()).is_false()


## Verify a reader is healthy again once it is pointed at a new buffer, so one
## truncated frame cannot poison the reader a later frame is decoded with.
func test_a_reset_clears_the_shortfall() -> void:
	var reader := NetwBitBufferReader.create(PackedByteArray())
	reader.get_aligned_u32()
	assert_bool(reader.ok()).is_false()

	var writer := NetwBitBufferWriter.new()
	writer.put_aligned_u16(7)
	reader.reset(writer.to_bytes())

	assert_bool(reader.ok()).is_true()
	assert_int(reader.get_aligned_u16()).is_equal(7)
	assert_bool(reader.ok()).is_true()


## Verify a byte read that reaches past the end reports it too, since a length
## prefix read out of invented zeros is how a truncated frame asks for bytes
## that were never sent.
func test_a_short_byte_read_reports_the_shortfall() -> void:
	var writer := NetwBitBufferWriter.new()
	writer.put_aligned_u8(1)
	var reader := NetwBitBufferReader.create(writer.to_bytes())

	var bytes := reader.get_aligned_bytes(8)

	assert_int(bytes.size()).is_equal(1)
	assert_bool(reader.ok()).is_false()
