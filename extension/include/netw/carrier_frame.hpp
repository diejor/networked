#pragma once

/* The datagram header the carrier puts in front of its frames.
 *
 * Three shapes, chosen by the first byte, because a datagram carries a
 * different amount of bookkeeping depending on what it can be answered with:
 *
 * [codeblock]
 *   reliable          [magic]                          1 byte
 *   unreliable        [magic][seq u16]                 3 bytes
 *   unreliable acked  [magic][seq u16][ack u16]        5 bytes
 * [/codeblock]
 *
 * The magic byte is what separates Networked traffic from the application's
 * own, which shares the same transport. That makes the read a three-way answer
 * rather than a two-way one: a packet can be somebody else's, or it can be
 * ours and malformed, and handing the second to the application as if it were
 * the first is how a truncated datagram becomes a game bug.
 *
 * The u16s are written a byte at a time rather than through the Variant
 * helpers, because `PackedByteArray::encode_u16` is a godot-cpp binding and
 * the engine's own `Vector<uint8_t>` has no such method.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwCarrierFrame : public godot::RefCounted {
    GDCLASS(NetwCarrierFrame, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // The first byte of each shape, published so the GDScript envelope reads
    // these rather than repeating them.
    enum Magic {
        MAGIC_RELIABLE = 0x56,
        MAGIC_UNRELIABLE = 0x76,
        MAGIC_UNRELIABLE_ACKED = 0x96,
    };

    enum Kind {
        // Not Networked traffic. The application owns this packet.
        FOREIGN,
        // Networked traffic this header cannot be read out of.
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

/* A datagram and the stamp it went out under, answered together.
 *
 * They come back as one value because the point of stamping at framing time is
 * that a caller never holds bytes whose sequence it has not been told. A send
 * pass stages its rows against this seq, so a shape where the bytes and the
 * number are two separate asks is a shape where they can disagree.
 *
 * `seq` is -1 for a reliable datagram, which carries no stamp at all.
 */
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
