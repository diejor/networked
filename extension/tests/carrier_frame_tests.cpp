// The carrier datagram header's laws.
//
// The round trip is half of it. The other half is the three-way read, and that
// half is the one with a bug behind it: the transport carries the
// application's packets too, so "not ours" and "ours and broken" are different
// answers, and a reader that collapses them hands a truncated Networked
// datagram to a game's own packet handler as if the game had sent it.

#include "support/netw_test.h"

#include "netw/carrier_frame.hpp"

namespace TestNetwCarrierFrame {

using godot::PackedByteArray;
using godot::Ref;
using netw::NetwCarrierFrame;

PackedByteArray payload(int p_size, uint8_t p_fill) {
    PackedByteArray out;
    out.resize(p_size);
    for (int i = 0; i < p_size; ++i) {
        out.set(i, p_fill);
    }
    return out;
}

// The bytes after the header, which is what the frame reader is handed.
PackedByteArray body(const PackedByteArray &p_packet, int64_t p_offset) {
    return p_packet.slice(p_offset);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F1 a reliable datagram carries no stamp, "
    "because the transport already orders it"
) {
    const PackedByteArray packet
        = NetwCarrierFrame::build(payload(8, 3), true, 41, 17);

    NETW_CHECK_EQ(packet.size(), 9);
    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(packet);
    NETW_CHECK_EQ(header->kind, NetwCarrierFrame::RELIABLE);
    NETW_CHECK_EQ(header->payload_offset, 1);
    NETW_CHECK_EQ(body(packet, header->payload_offset).size(), 8);

    // The seq and ack handed in are ignored rather than written, so a caller
    // passing them cannot make a reliable datagram lie about its shape.
    NETW_CHECK_EQ(header->seq, 0);
    NETW_CHECK_EQ(header->ack, 0);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F2 an unheard peer gets the short shape and "
    "an answered one gets the echo"
) {
    const PackedByteArray unheard
        = NetwCarrierFrame::build(payload(4, 1), false, 9, -1);
    NETW_CHECK_EQ(unheard.size(), 7);
    const Ref<NetwCarrierFrame> short_header = NetwCarrierFrame::read(unheard);
    NETW_CHECK_EQ(short_header->kind, NetwCarrierFrame::UNRELIABLE);
    NETW_CHECK_EQ(short_header->seq, 9);
    NETW_CHECK_EQ(short_header->payload_offset, 3);

    const PackedByteArray answered
        = NetwCarrierFrame::build(payload(4, 1), false, 9, 12);
    NETW_CHECK_EQ(answered.size(), 9);
    const Ref<NetwCarrierFrame> acked = NetwCarrierFrame::read(answered);
    NETW_CHECK_EQ(acked->kind, NetwCarrierFrame::UNRELIABLE_ACKED);
    NETW_CHECK_EQ(acked->seq, 9);
    NETW_CHECK_EQ(acked->ack, 12);
    NETW_CHECK_EQ(acked->payload_offset, 5);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F3 the stamps survive the whole u16, "
    "including the wrap the sequence books count on"
) {
    const int64_t stamps[] = {0, 1, 255, 256, 32767, 32768, 65534, 65535};
    for (int64_t stamp : stamps) {
        NETW_FORMAT_INT(stamp_text, stamp);
        CAPTURE(stamp_text);
        const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(
            NetwCarrierFrame::build(PackedByteArray(), false, stamp, stamp)
        );
        NETW_CHECK_EQ(header->seq, stamp);
        NETW_CHECK_EQ(header->ack, stamp);
    }
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F4 somebody else's packet is foreign, and "
    "ours-but-broken is not"
) {
    // The application shares this transport, so an unknown first byte is not
    // an error, it is somebody's data.
    PackedByteArray theirs = payload(6, 0x11);
    NETW_CHECK_EQ(NetwCarrierFrame::read(theirs)->kind, NetwCarrierFrame::FOREIGN);

    // A packet claiming our framing and too short to carry its own header is
    // ours and broken. Answering FOREIGN here is what would put a truncated
    // datagram into a game's peer_packet handler.
    ERR_PRINT_OFF;
    PackedByteArray truncated;
    truncated.resize(2);
    truncated.set(0, 0x76);
    truncated.set(1, 0);
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(truncated)->kind,
        NetwCarrierFrame::MALFORMED
    );

    PackedByteArray truncated_ack;
    truncated_ack.resize(4);
    truncated_ack.set(0, 0x96);
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(truncated_ack)->kind,
        NetwCarrierFrame::MALFORMED
    );

    // An empty packet claims nothing and cannot be handed on either.
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(PackedByteArray())->kind,
        NetwCarrierFrame::MALFORMED
    );
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F5 a header with no frames behind it is "
    "whole, which is what a standalone ack is"
) {
    const PackedByteArray packet
        = NetwCarrierFrame::build(PackedByteArray(), false, 4, 8);

    NETW_CHECK_EQ(packet.size(), 5);
    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(packet);
    NETW_CHECK_EQ(header->kind, NetwCarrierFrame::UNRELIABLE_ACKED);
    NETW_CHECK_EQ(header->seq, 4);
    NETW_CHECK_EQ(header->ack, 8);
    NETW_CHECK_EQ(body(packet, header->payload_offset).size(), 0);
}

} // namespace TestNetwCarrierFrame
