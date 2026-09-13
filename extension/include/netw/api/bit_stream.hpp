#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/wire/stream.hpp"

namespace netw {

class NetwBitStream : public godot::RefCounted {
    GDCLASS(NetwBitStream, godot::RefCounted)

public:
    enum Mode {
        WRITE = 0,
        READ = 1,
        MEASURE = 2,
    };

private:
    Mode mode = WRITE;
    wire::WriteStream writing;
    wire::ReadStream reading;
    wire::MeasureStream measuring;

    template <class Fn> bool run(Fn &&p_fn) {
        switch (mode) {
            case READ:
                return p_fn(reading);
            case MEASURE:
                return p_fn(measuring);
            default:
                return p_fn(writing);
        }
    }

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwBitStream> writer();
    static godot::Ref<NetwBitStream> reader(
        const godot::PackedByteArray &p_bytes
    );
    static godot::Ref<NetwBitStream> measurer();

    Mode get_mode() const {
        return mode;
    }

    wire::ReadStream *read_stream() {
        return mode == READ ? &reading : nullptr;
    }

    int64_t bits(int64_t p_value, int p_count);
    int64_t int_range(int64_t p_value, int64_t p_low, int64_t p_high);
    int64_t varuint(int64_t p_value, int p_max_bytes);
    int64_t svarint(int64_t p_value, int p_max_bytes);
    bool bool1(bool p_value);
    godot::PackedByteArray bytes_capped(
        const godot::PackedByteArray &p_value,
        int p_cap
    );
    godot::String string(const godot::String &p_value);
    bool align_verify();

    bool ok();
    int64_t bit_length();
    int64_t bits_remaining();
    godot::PackedByteArray to_bytes() const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwBitStream::Mode)
