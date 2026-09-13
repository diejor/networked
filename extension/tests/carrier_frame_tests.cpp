#include "support/netw_test.h"

#include "netw/carrier_frame.hpp"
#include "netw/datagram_seq_book.hpp"

using namespace godot;

namespace TestNetwCarrierFrame {

using godot::PackedByteArray;
using godot::Ref;
using netw::DatagramSeqBook;
using netw::NetwCarrierFrame;

PackedByteArray payload(int p_size, uint8_t p_fill) {
    PackedByteArray out;
    out.resize(p_size);
    for (int i = 0; i < p_size; ++i) {
        out.set(i, p_fill);
    }
    return out;
}

PackedByteArray body(const PackedByteArray &p_packet, int64_t p_offset) {
    return p_packet.slice(p_offset);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F1 a reliable datagram carries no stamp, "
    "because the transport already orders it"
) {
    const PackedByteArray packet
        = NetwCarrierFrame::build(payload(8, 3), true, 41, 17, 0xFFFF, 5);

    NETW_CHECK_EQ(packet.size(), 10);
    const NetwCarrierFrame header = NetwCarrierFrame::read(packet);
    NETW_CHECK_EQ(header.kind, NetwCarrierFrame::RELIABLE);
    NETW_CHECK_EQ(header.payload_offset, 2);
    NETW_CHECK_EQ(body(packet, header.payload_offset).size(), 8);

    NETW_CHECK_EQ(header.seq, 0);
    NETW_CHECK_EQ(header.ack, 0);
    NETW_CHECK_EQ(int64_t(header.history), int64_t(0));
    NETW_CHECK_EQ(header.tick, 5);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F2 an unheard peer gets the short shape and "
    "an answered one gets the echo beside its history"
) {
    const PackedByteArray unheard
        = NetwCarrierFrame::build(payload(4, 1), false, 9, -1, 0, 0);
    NETW_CHECK_EQ(unheard.size(), 8);
    const NetwCarrierFrame short_header = NetwCarrierFrame::read(unheard);
    NETW_CHECK_EQ(short_header.kind, NetwCarrierFrame::UNRELIABLE);
    NETW_CHECK_EQ(short_header.seq, 9);
    NETW_CHECK_EQ(short_header.payload_offset, 4);
    NETW_CHECK_EQ(short_header.tick, 0);

    const PackedByteArray answered
        = NetwCarrierFrame::build(payload(4, 1), false, 9, 12, 0xA5A5A5A5, 7);
    NETW_CHECK_EQ(answered.size(), 14);
    const NetwCarrierFrame acked = NetwCarrierFrame::read(answered);
    NETW_CHECK_EQ(acked.kind, NetwCarrierFrame::UNRELIABLE_ACKED);
    NETW_CHECK_EQ(acked.seq, 9);
    NETW_CHECK_EQ(acked.ack, 12);
    NETW_CHECK_EQ(int64_t(acked.history), int64_t(0xA5A5A5A5));
    NETW_CHECK_EQ(acked.tick, 7);
    NETW_CHECK_EQ(acked.payload_offset, 10);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F3 the stamps survive the whole u16, "
    "including the wrap the sequence books count on"
) {
    const int64_t stamps[] = {0, 1, 255, 256, 32767, 32768, 65534, 65535};
    for (int64_t stamp : stamps) {
        NETW_FORMAT_INT(stamp_text, stamp);
        CAPTURE(stamp_text);
        const NetwCarrierFrame header = NetwCarrierFrame::read(
            NetwCarrierFrame::build(
                PackedByteArray(),
                false,
                stamp,
                stamp,
                0,
                -1
            )
        );
        NETW_CHECK_EQ(header.seq, stamp);
        NETW_CHECK_EQ(header.ack, stamp);
    }
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F4 somebody else's packet is foreign, and "
    "ours-but-broken is not"
) {
    PackedByteArray theirs = payload(6, 0x11);
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(theirs).kind,
        NetwCarrierFrame::FOREIGN
    );

    ERR_PRINT_OFF;
    PackedByteArray truncated;
    truncated.resize(2);
    truncated.set(0, NetwCarrierFrame::MAGIC_UNRELIABLE);
    truncated.set(1, 0);
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(truncated).kind,
        NetwCarrierFrame::MALFORMED
    );

    PackedByteArray truncated_ack;
    truncated_ack.resize(4);
    truncated_ack.set(0, NetwCarrierFrame::MAGIC_UNRELIABLE_ACKED);
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(truncated_ack).kind,
        NetwCarrierFrame::MALFORMED
    );

    NETW_CHECK_EQ(
        NetwCarrierFrame::read(PackedByteArray()).kind,
        NetwCarrierFrame::MALFORMED
    );
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F5 a header with no frames behind it is "
    "whole, which is what a standalone ack is"
) {
    const PackedByteArray packet
        = NetwCarrierFrame::build(PackedByteArray(), false, 4, 8, 3, 0);

    NETW_CHECK_EQ(packet.size(), 10);
    const NetwCarrierFrame header = NetwCarrierFrame::read(packet);
    NETW_CHECK_EQ(header.kind, NetwCarrierFrame::UNRELIABLE_ACKED);
    NETW_CHECK_EQ(header.seq, 4);
    NETW_CHECK_EQ(header.ack, 8);
    NETW_CHECK_EQ(body(packet, header.payload_offset).size(), 0);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F6 a retired magic is ours and broken, "
    "because a byte that was ours one version ago is not a game's data"
) {
    ERR_PRINT_OFF;
    const uint8_t retired[] = {0x4E, 0x6E, 0x8E, 0x56, 0x76, 0x96};
    for (const uint8_t magic : retired) {
        NETW_FORMAT_INT(magic_text, int64_t(magic));
        CAPTURE(magic_text);
        PackedByteArray packet = payload(12, 0);
        packet.set(0, magic);
        const NetwCarrierFrame header = NetwCarrierFrame::read(packet);
        const bool refused = header.kind == NetwCarrierFrame::MALFORMED;
        CHECK(refused);
        const bool no_offset = header.payload_offset == 0;
        CHECK(no_offset);
    }
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F7 an unconfigured clock rides as no tick "
    "rather than as tick zero"
) {
    const NetwCarrierFrame unconfigured = NetwCarrierFrame::read(
        NetwCarrierFrame::build(PackedByteArray(), false, 1, -1, 0, -1)
    );
    NETW_CHECK_EQ(unconfigured.tick, -1);

    const NetwCarrierFrame zero = NetwCarrierFrame::read(
        NetwCarrierFrame::build(PackedByteArray(), false, 1, -1, 0, 0)
    );
    NETW_CHECK_EQ(zero.tick, 0);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F8 the history bit for a dropped seq reads "
    "zero, which is what makes a loss measurable at all"
) {
    DatagramSeqBook book;
    for (const uint16_t seq : {uint16_t(1), uint16_t(2), uint16_t(4)}) {
        book.note_inbound(7, seq);
    }

    NETW_CHECK_EQ(int64_t(book.inbound_seq(7)), int64_t(4));
    const uint32_t history = book.inbound_delivery_history(7);
    const bool three_is_lost = (history & (1u << 0)) == 0;
    CHECK(three_is_lost);
    const bool two_arrived = (history & (1u << 1)) != 0;
    CHECK(two_arrived);
    const bool one_arrived = (history & (1u << 2)) != 0;
    CHECK(one_arrived);

    book.note_inbound(7, 3);
    const uint32_t repaired = book.inbound_delivery_history(7);
    const bool three_now_reads_arrived = (repaired & (1u << 0)) != 0;
    CHECK(three_now_reads_arrived);
    NETW_CHECK_EQ(int64_t(book.inbound_seq(7)), int64_t(4));
}

TEST_CASE(
    "[Networked][Carrier][Hosted] F9 a gap wider than the window clears the "
    "history, because a bit nobody set must not read as delivered"
) {
    DatagramSeqBook book;
    book.note_inbound(3, 1);
    book.note_inbound(3, 2);
    book.note_inbound(3, 40);

    NETW_CHECK_EQ(int64_t(book.inbound_delivery_history(3)), int64_t(0));
}

} // namespace TestNetwCarrierFrame
