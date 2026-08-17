#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwBitBufferWriter : public godot::RefCounted {
    GDCLASS(NetwBitBufferWriter, godot::RefCounted)

    godot::PackedByteArray output;
    uint64_t accumulator = 0;
    int bit_count = 0;

protected:
    static void _bind_methods();

public:
    void reset();
    void put_bits(int64_t value, int count);
    void align();
    void put_aligned_u8(int64_t value);
    void put_aligned_u16(int64_t value);
    void put_aligned_u32(int64_t value);
    void put_aligned_bytes(const godot::PackedByteArray &bytes);
    godot::PackedByteArray to_bytes();
};

class NetwBitBufferReader : public godot::RefCounted {
    GDCLASS(NetwBitBufferReader, godot::RefCounted)

    godot::PackedByteArray input;
    int64_t position = 0;
    uint64_t accumulator = 0;
    int bit_count = 0;
    bool healthy = true;

    uint8_t take_byte();

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwBitBufferReader> create(
        const godot::PackedByteArray &bytes
    );
    void reset(const godot::PackedByteArray &bytes);
    int64_t get_bits(int count);
    void align();
    int64_t get_aligned_u8();
    int64_t get_aligned_u16();
    int64_t get_aligned_u32();
    godot::PackedByteArray get_aligned_bytes(int count);
    int remaining_bytes();

    bool ok() const {
        return healthy;
    }
};

} // namespace netw
