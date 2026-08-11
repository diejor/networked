#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw::wire {

// Bits needed to hold every value in [0, range]. Zero when the range holds one
// value: a field with one legal value costs nothing at all, which is not the
// same as costing a zero.
constexpr int bits_required(uint64_t range) {
    int width = 0;
    while (range > 0) {
        width += 1;
        range >>= 1;
    }
    return width;
}

// One serialize description, instantiated three ways. WriteStream, ReadStream
// and MeasureStream expose the same vocabulary with the same signatures, so a
// format is written once as a function template over the stream and the three
// modes cannot drift apart.
//
// Writing asserts and reading validates. A value outside its declared range is
// our own bug on the way out and someone else's packet on the way in, so the
// write refuses in a debug build while the read poisons the stream.
//
// A failed read poisons the stream, and every call after that is a no-op which
// leaves its argument untouched. So a caller checks ok() once when it has
// finished decoding rather than after each field.
//
// Running out of bits is a failure and never a value. A reader that serves
// zeros past the end of its buffer turns a truncated frame into a plausible
// one, which is the failure this contract exists to prevent.
//
// MeasureStream answers what a value would cost without producing it, which is
// what lets a caller decide whether a frame fits a budget before committing to
// building it.
//
// Bit order is LSB-first within a byte, matching NetwBitBufferWriter, so the
// quantizer family packs bit-continuously into either.

class WriteStream {
    // The partial final byte lives in the buffer already, zero above the
    // cursor, so a flush is a copy and there is no accumulator to forget.
    godot::LocalVector<uint8_t> output;
    int64_t bits_written = 0;
    bool healthy = true;

public:
    static constexpr bool is_writing = true;
    static constexpr bool is_reading = false;
    static constexpr bool is_measuring = false;

    bool bits(uint64_t &value, int count);
    bool int_range(int64_t &value, int64_t low, int64_t high);
    bool varuint(uint64_t &value, int max_bytes = 10);
    bool svarint(int64_t &value, int max_bytes = 10);
    bool bool1(bool &value);
    bool bytes_capped(godot::PackedByteArray &value, int cap);
    bool align_verify();

    bool ok() const {
        return healthy;
    }
    int64_t bit_length() const;
    godot::PackedByteArray to_bytes() const;
};

class ReadStream {
    // The datagram is held rather than pointed at, so a stream cannot outlive
    // the bytes it reads.
    godot::PackedByteArray source;
    const uint8_t *input = nullptr;
    int64_t input_size = 0;
    int64_t bits_read = 0;
    bool healthy = true;

    bool take(int count, uint64_t &out);

public:
    static constexpr bool is_writing = false;
    static constexpr bool is_reading = true;
    static constexpr bool is_measuring = false;

    explicit ReadStream(const godot::PackedByteArray &bytes);

    bool bits(uint64_t &value, int count);
    bool int_range(int64_t &value, int64_t low, int64_t high);
    bool varuint(uint64_t &value, int max_bytes = 10);
    bool svarint(int64_t &value, int max_bytes = 10);
    bool bool1(bool &value);
    bool bytes_capped(godot::PackedByteArray &value, int cap);
    bool align_verify();

    bool ok() const {
        return healthy;
    }
    int64_t bit_length() const {
        return bits_read;
    }

    // Bits the datagram still holds. Decode-to-exhaustion means a frame that
    // leaves any of them unread is a frame that did not decode.
    int64_t bits_remaining() const {
        return input_size * 8 - bits_read;
    }
};

class MeasureStream {
    int64_t bits_described = 0;

public:
    static constexpr bool is_writing = false;
    static constexpr bool is_reading = false;
    static constexpr bool is_measuring = true;

    bool bits(uint64_t &value, int count);
    bool int_range(int64_t &value, int64_t low, int64_t high);
    bool varuint(uint64_t &value, int max_bytes = 10);
    bool svarint(int64_t &value, int max_bytes = 10);
    bool bool1(bool &value);
    bool bytes_capped(godot::PackedByteArray &value, int cap);
    bool align_verify();

    bool ok() const {
        return true;
    }
    int64_t bit_length() const {
        return bits_described;
    }

    // What the fitter actually asks: whole bytes this frame would occupy.
    int64_t byte_length() const {
        return (bits_described + 7) / 8;
    }
};

} // namespace netw::wire
