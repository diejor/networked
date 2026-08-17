#include "netw/carrier_frame.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

namespace netw {

using namespace godot;

namespace {

void write_u16(PackedByteArray &p_out, int64_t p_at, int64_t p_value) {
    const uint16_t value = uint16_t(p_value & 0xFFFF);
    p_out.set(p_at, uint8_t(value & 0xFF));
    p_out.set(p_at + 1, uint8_t((value >> 8) & 0xFF));
}

int64_t read_u16(const PackedByteArray &p_bytes, int64_t p_at) {
    return int64_t(uint16_t(p_bytes[p_at]))
        | (int64_t(uint16_t(p_bytes[p_at + 1])) << 8);
}

} // namespace

PackedByteArray NetwCarrierFrame::build(
    const PackedByteArray &p_payload,
    bool p_reliable,
    int64_t p_seq,
    int64_t p_ack
) {
    PackedByteArray out;
    if (p_reliable) {
        out.resize(1);
        out.set(0, uint8_t(MAGIC_RELIABLE));
    } else if (p_ack < 0) {
        out.resize(3);
        out.set(0, uint8_t(MAGIC_UNRELIABLE));
        write_u16(out, 1, p_seq);
    } else {
        out.resize(5);
        out.set(0, uint8_t(MAGIC_UNRELIABLE_ACKED));
        write_u16(out, 1, p_seq);
        write_u16(out, 3, p_ack);
    }
    out.append_array(p_payload);
    return out;
}

Ref<NetwCarrierFrame> NetwCarrierFrame::read(const PackedByteArray &p_packet) {
    Ref<NetwCarrierFrame> out;
    out.instantiate();
    if (p_packet.is_empty()) {
        out->kind = MALFORMED;
        return out;
    }

    const uint8_t magic = p_packet[0];
    int64_t header = 0;
    if (magic == MAGIC_RELIABLE) {
        out->kind = RELIABLE;
        header = 1;
    } else if (magic == MAGIC_UNRELIABLE) {
        out->kind = UNRELIABLE;
        header = 3;
    } else if (magic == MAGIC_UNRELIABLE_ACKED) {
        out->kind = UNRELIABLE_ACKED;
        header = 5;
    } else {
        out->kind = FOREIGN;
        return out;
    }

    // A packet that claims our framing and cannot carry its own header is
    // ours and broken, never the application's. Handing it back as foreign
    // would put a truncated datagram into a game's peer_packet handler.
    if (int64_t(p_packet.size()) < header) {
        NETW_WARN_ONCE(
            sys::TRANSPORT,
            "carrier packet of %d bytes cannot carry its %d byte header",
            p_packet.size(),
            header
        );
        out->kind = MALFORMED;
        return out;
    }

    if (out->kind != RELIABLE) {
        out->seq = read_u16(p_packet, 1);
    }
    if (out->kind == UNRELIABLE_ACKED) {
        out->ack = read_u16(p_packet, 3);
    }
    out->payload_offset = header;
    return out;
}

void NetwCarrierFrame::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwCarrierFrame",
        D_METHOD("build", "payload", "reliable", "seq", "ack"),
        &NetwCarrierFrame::build
    );
    ClassDB::bind_static_method(
        "NetwCarrierFrame",
        D_METHOD("read", "packet"),
        &NetwCarrierFrame::read
    );
    ClassDB::bind_method(D_METHOD("get_kind"), &NetwCarrierFrame::get_kind);
    ClassDB::bind_method(D_METHOD("get_seq"), &NetwCarrierFrame::get_seq);
    ClassDB::bind_method(D_METHOD("get_ack"), &NetwCarrierFrame::get_ack);
    ClassDB::bind_method(
        D_METHOD("get_payload_offset"),
        &NetwCarrierFrame::get_payload_offset
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind"), "", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seq"), "", "get_seq");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ack"), "", "get_ack");
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "payload_offset"),
        "",
        "get_payload_offset"
    );

    BIND_ENUM_CONSTANT(MAGIC_RELIABLE);
    BIND_ENUM_CONSTANT(MAGIC_UNRELIABLE);
    BIND_ENUM_CONSTANT(MAGIC_UNRELIABLE_ACKED);

    BIND_ENUM_CONSTANT(FOREIGN);
    BIND_ENUM_CONSTANT(MALFORMED);
    BIND_ENUM_CONSTANT(RELIABLE);
    BIND_ENUM_CONSTANT(UNRELIABLE);
    BIND_ENUM_CONSTANT(UNRELIABLE_ACKED);
}

void NetwCarrierDatagram::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_bytes"), &NetwCarrierDatagram::get_bytes);
    ClassDB::bind_method(D_METHOD("get_seq"), &NetwCarrierDatagram::get_seq);
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes"),
        "",
        "get_bytes"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seq"), "", "get_seq");
}

} // namespace netw
