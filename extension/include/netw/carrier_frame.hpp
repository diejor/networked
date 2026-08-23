#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwCarrierFrame : public godot::RefCounted {
    GDCLASS(NetwCarrierFrame, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    enum Magic {
        MAGIC_RELIABLE = 0x56,
        MAGIC_UNRELIABLE = 0x76,
        MAGIC_UNRELIABLE_ACKED = 0x96,
    };

    enum Kind {
        FOREIGN,
        MALFORMED,
        RELIABLE,
        UNRELIABLE,
        UNRELIABLE_ACKED,
    };

    static godot::PackedByteArray build(
        const godot::PackedByteArray &p_payload,
        bool p_reliable,
        int64_t p_seq,
        int64_t p_ack
    );

    static godot::Ref<NetwCarrierFrame> read(
        const godot::PackedByteArray &p_packet
    );

    int64_t kind = FOREIGN;
    int64_t seq = 0;
    int64_t ack = 0;
    int64_t payload_offset = 0;

    int64_t get_kind() const { return kind; }
    int64_t get_seq() const { return seq; }
    int64_t get_ack() const { return ack; }
    int64_t get_payload_offset() const { return payload_offset; }
};

class NetwCarrierDatagram : public godot::RefCounted {
    GDCLASS(NetwCarrierDatagram, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::PackedByteArray bytes;
    int64_t seq = -1;

    godot::PackedByteArray get_bytes() const { return bytes; }
    int64_t get_seq() const { return seq; }
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwCarrierFrame::Kind);
VARIANT_ENUM_CAST(netw::NetwCarrierFrame::Magic);
