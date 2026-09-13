#include "netw/carrier_frame.hpp"

#include "netw/log.hpp"
#include "netw/wire/stream.hpp"

namespace netw {

using namespace godot;

namespace {

constexpr uint8_t RETIRED_MAGICS[] = {0x4E, 0x6E, 0x8E, 0x56, 0x76, 0x96};

bool run_head(
    wire::WriteStream &p_stream,
    int64_t p_kind,
    DatagramHead &r_head
) {
    switch (p_kind) {
        case NetwCarrierFrame::RELIABLE:
            return DatagramHead::reliable_wire.run(p_stream, r_head);
        case NetwCarrierFrame::UNRELIABLE:
            return DatagramHead::unreliable_wire.run(p_stream, r_head);
        default:
            return DatagramHead::acked_wire.run(p_stream, r_head);
    }
}

bool run_head(
    wire::ReadStream &p_stream,
    int64_t p_kind,
    DatagramHead &r_head
) {
    switch (p_kind) {
        case NetwCarrierFrame::RELIABLE:
            return DatagramHead::reliable_wire.run(p_stream, r_head);
        case NetwCarrierFrame::UNRELIABLE:
            return DatagramHead::unreliable_wire.run(p_stream, r_head);
        default:
            return DatagramHead::acked_wire.run(p_stream, r_head);
    }
}

} // namespace

bool NetwCarrierFrame::magic_is_retired(uint8_t p_magic) {
    for (const uint8_t retired : RETIRED_MAGICS) {
        if (p_magic == retired) {
            return true;
        }
    }
    return false;
}

PackedByteArray NetwCarrierFrame::build(
    const PackedByteArray &p_payload,
    bool p_reliable,
    int64_t p_seq,
    int64_t p_ack,
    uint32_t p_history,
    int64_t p_tick
) {
    DatagramHead head;
    head.base_tick = p_tick < 0 ? uint64_t(0) : uint64_t(p_tick) + 1;

    int64_t kind = RELIABLE;
    head.magic = uint64_t(MAGIC_RELIABLE);
    if (!p_reliable) {
        head.seq = uint64_t(uint16_t(p_seq));
        if (p_ack < 0) {
            kind = UNRELIABLE;
            head.magic = uint64_t(MAGIC_UNRELIABLE);
        } else {
            kind = UNRELIABLE_ACKED;
            head.magic = uint64_t(MAGIC_UNRELIABLE_ACKED);
            head.ack = uint64_t(uint16_t(p_ack));
            head.history = uint64_t(p_history);
        }
    }

    wire::WriteStream stream;
    if (!run_head(stream, kind, head) || !stream.align_verify()
        || !stream.ok()) {
        NETW_WARN_ONCE(
            sys::TRANSPORT,
            "a carrier datagram at tick %d could not write its own header",
            int(p_tick)
        );
        return PackedByteArray();
    }
    PackedByteArray out = stream.to_bytes();
    out.append_array(p_payload);
    return out;
}

NetwCarrierFrame NetwCarrierFrame::read(const PackedByteArray &p_packet) {
    NetwCarrierFrame out;
    if (p_packet.is_empty()) {
        out.kind = MALFORMED;
        return out;
    }

    const uint8_t magic = p_packet[0];
    if (magic == MAGIC_RELIABLE) {
        out.kind = RELIABLE;
    } else if (magic == MAGIC_UNRELIABLE) {
        out.kind = UNRELIABLE;
    } else if (magic == MAGIC_UNRELIABLE_ACKED) {
        out.kind = UNRELIABLE_ACKED;
    } else if (magic_is_retired(magic)) {
        NETW_WARN_ONCE(
            sys::TRANSPORT,
            "a carrier packet opens with the retired magic 0x%x, so it speaks "
            "a format this build cannot read",
            int(magic)
        );
        out.kind = MALFORMED;
        return out;
    } else {
        out.kind = FOREIGN;
        return out;
    }

    wire::ReadStream stream(p_packet);
    DatagramHead head;
    if (!run_head(stream, out.kind, head) || !stream.align_verify()
        || !stream.ok()) {
        NETW_WARN_ONCE(
            sys::TRANSPORT,
            "a carrier packet of %d bytes cannot carry the header its magic "
            "0x%x claims",
            p_packet.size(),
            int(magic)
        );
        NetwCarrierFrame refused;
        refused.kind = MALFORMED;
        return refused;
    }

    if (out.kind != RELIABLE) {
        out.seq = int64_t(head.seq);
    }
    if (out.kind == UNRELIABLE_ACKED) {
        out.ack = int64_t(head.ack);
        out.history = uint32_t(head.history);
    }
    out.tick = head.base_tick == 0 ? int64_t(-1) : int64_t(head.base_tick) - 1;
    out.payload_offset = stream.bit_length() / 8;
    return out;
}

Dictionary NetwCarrierFrame::spec_records() {
    Dictionary out;
    out["DatagramReliable"] = DatagramHead::reliable_wire.spec_dump();
    out["DatagramUnreliable"] = DatagramHead::unreliable_wire.spec_dump();
    out["DatagramAcked"] = DatagramHead::acked_wire.spec_dump();
    return out;
}

} // namespace netw
