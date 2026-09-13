#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw::wire {

constexpr int bits_required(uint64_t range) {
    int width = 0;
    while (range > 0) {
        width += 1;
        range >>= 1;
    }
    return width;
}

class WriteStream {
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
    bool raw_bytes(godot::PackedByteArray &value, int64_t count);
    bool align_verify();

    bool ok() const {
        return healthy;
    }
    int64_t bit_length() const;
    godot::PackedByteArray to_bytes() const;
};

class ReadStream {
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

    ReadStream() = default;
    explicit ReadStream(const godot::PackedByteArray &bytes);

    void seat(const godot::PackedByteArray &bytes);

    bool bits(uint64_t &value, int count);
    bool int_range(int64_t &value, int64_t low, int64_t high);
    bool varuint(uint64_t &value, int max_bytes = 10);
    bool svarint(int64_t &value, int max_bytes = 10);
    bool bool1(bool &value);
    bool bytes_capped(godot::PackedByteArray &value, int cap);
    bool raw_bytes(godot::PackedByteArray &value, int64_t count);
    bool align_verify();

    bool ok() const {
        return healthy;
    }
    int64_t bit_length() const {
        return bits_read;
    }

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
    bool raw_bytes(godot::PackedByteArray &value, int64_t count);
    bool align_verify();

    bool ok() const {
        return true;
    }
    int64_t bit_length() const {
        return bits_described;
    }

    int64_t byte_length() const {
        return (bits_described + 7) / 8;
    }
};

constexpr int STRING_CAP = 1023;

template <class Stream>
inline bool string_field(Stream &p_stream, godot::String &r_value) {
    godot::PackedByteArray staged;
    if constexpr (!Stream::is_reading) {
        staged = r_value.to_utf8_buffer();
    }
    if (!p_stream.bytes_capped(staged, STRING_CAP)) {
        return false;
    }
    if constexpr (Stream::is_reading) {
        r_value = gd::utf8_string(staged);
    }
    return true;
}

} // namespace netw::wire
