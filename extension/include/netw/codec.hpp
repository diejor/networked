#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/bit_buffer.hpp"
#include "netw/quantize.hpp"

namespace netw {

class NetwCodec : public godot::RefCounted {
    GDCLASS(NetwCodec, godot::RefCounted)

public:
    enum RawType {
        T_FALLBACK = 0,
        T_BOOL = 1,
        T_INT = 2,
        T_VECTOR2 = 3,
        T_FLOAT = 4,
        T_VECTOR3 = 5,
    };

private:
    static godot::Ref<NetwQuantize> quantizer_at(
        const godot::Array &quantizers,
        int index
    );
    static int type_at(const godot::Array &types, int index);
    static int raw_type_of(const godot::Variant &value);
    static int64_t encode_zigzag(int64_t value);
    static int64_t decode_zigzag(int64_t value);
    static void put_svarint(
        const godot::Ref<NetwBitBufferWriter> &writer,
        int64_t value
    );

protected:
    static void _bind_methods();

public:
    static godot::PackedByteArray encode_snapshot(
        int64_t tick,
        int64_t ack,
        const godot::Dictionary &payload,
        const godot::Array &keys,
        const godot::Array &quantizers
    );
    static godot::Dictionary decode_snapshot(
        const godot::PackedByteArray &bytes,
        const godot::Array &keys,
        const godot::Array &quantizers,
        const godot::Array &types
    );
    static godot::PackedByteArray encode_window(
        const godot::Array &samples,
        const godot::Array &keys,
        const godot::Array &quantizers
    );
    static godot::Array decode_window(
        const godot::PackedByteArray &bytes,
        const godot::Array &keys,
        const godot::Array &quantizers,
        const godot::Array &types
    );
    static void encode_payload(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Dictionary &payload,
        const godot::Array &keys,
        const godot::Array &quantizers
    );
    static godot::Dictionary decode_payload(
        const godot::Ref<NetwBitBufferReader> &reader,
        const godot::Array &keys,
        const godot::Array &quantizers,
        const godot::Array &types
    );
    static void encode_value(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value,
        const godot::Ref<NetwQuantize> &quantizer
    );
    static godot::Variant decode_value(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type,
        const godot::Ref<NetwQuantize> &quantizer
    );
    static void put_varint(
        const godot::Ref<NetwBitBufferWriter> &writer,
        int64_t value
    );
    static int64_t get_safe_varint(
        const godot::Ref<NetwBitBufferReader> &reader
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwCodec::RawType)
