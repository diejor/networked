#include "support/netw_test.h"

#include "netw/wire/frame.hpp"

using namespace godot;

namespace TestNetwWireFrame {

using godot::PackedByteArray;
using godot::String;

PackedByteArray bytes(const std::initializer_list<uint8_t> &p_values) {
    PackedByteArray out;
    for (const uint8_t value : p_values) {
        out.push_back(value);
    }
    return out;
}

PackedByteArray filled(int p_size, uint8_t p_fill) {
    PackedByteArray out;
    out.resize(p_size);
    for (int index = 0; index < p_size; ++index) {
        out.set(index, p_fill);
    }
    return out;
}

TEST_CASE(
    "[Networked][Wire][Hosted] W1 a frame is route, comp, channel, length "
    "and payload, in that order and with nothing between them"
) {
    const PackedByteArray framed
        = netw::wire::frame_pack(7, 3, 100, bytes({9, 9}), String());

    NETW_CHECK_EQ(framed.size(), 6);
    NETW_CHECK_EQ(framed[0], 7);
    NETW_CHECK_EQ(framed[1], 3);
    NETW_CHECK_EQ(framed[2], 100);
    NETW_CHECK_EQ(framed[3], 2);
    NETW_CHECK_EQ(framed[4], 9);
    NETW_CHECK_EQ(framed[5], 9);
}

TEST_CASE(
    "[Networked][Wire][Hosted] W2 a route past 127 takes a second varint "
    "byte, so the reader that walks a datagram splits at the right frame"
) {
    const PackedByteArray framed
        = netw::wire::frame_pack(300, 0, 19, bytes({1}), String());

    NETW_CHECK_EQ(framed.size(), 6);
    NETW_CHECK_EQ(framed[0], 0xac);
    NETW_CHECK_EQ(framed[1], 0x02);
    NETW_CHECK_EQ(framed[2], 0);
    NETW_CHECK_EQ(framed[3], 19);
    NETW_CHECK_EQ(framed[4], 1);
    NETW_CHECK_EQ(framed[5], 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] W3 a payload past 127 bytes takes a two byte "
    "length, and the length counts the payload rather than the frame"
) {
    const PackedByteArray framed
        = netw::wire::frame_pack(1, 0, 19, filled(200, 0xab), String());

    NETW_CHECK_EQ(framed.size(), 205);
    NETW_CHECK_EQ(framed[3], 0xc8);
    NETW_CHECK_EQ(framed[4], 0x01);
    NETW_CHECK_EQ(framed[5], 0xab);
    NETW_CHECK_EQ(framed[204], 0xab);
}

TEST_CASE(
    "[Networked][Wire][Hosted] W4 comp 255 carries the node path inside the "
    "length prefixed body, so the path cannot be mistaken for a next frame"
) {
    const PackedByteArray framed = netw::wire::frame_pack(
        2,
        netw::wire::FRAME_COMP_PATH,
        5,
        bytes({7}),
        String("ab")
    );

    NETW_CHECK_EQ(framed.size(), 9);
    NETW_CHECK_EQ(framed[0], 2);
    NETW_CHECK_EQ(framed[1], 255);
    NETW_CHECK_EQ(framed[2], 5);
    NETW_CHECK_EQ(framed[3], 5);
    NETW_CHECK_EQ(framed[4], 2);
    NETW_CHECK_EQ(framed[5], 0);
    NETW_CHECK_EQ(framed[6], 'a');
    NETW_CHECK_EQ(framed[7], 'b');
    NETW_CHECK_EQ(framed[8], 7);
}

TEST_CASE(
    "[Networked][Wire][Hosted] W5 an empty payload still carries its zero "
    "length, so a frame with nothing to say is still a frame"
) {
    const PackedByteArray framed
        = netw::wire::frame_pack(0, 0, 21, PackedByteArray(), String());

    NETW_CHECK_EQ(framed.size(), 4);
    NETW_CHECK_EQ(framed[3], 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] W6 a route past the varint bound produces no "
    "frame at all, because a fifth group carrying its continuation bit is "
    "malformed rather than truncated and eats the next field's first byte"
) {
    const PackedByteArray framed
        = netw::wire::frame_pack(int64_t(1) << 35, 0, 19, bytes({1}), String());

    NETW_CHECK_EQ(framed.size(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] W7 the envelope transcribed from WIRE.md 9.2 "
    "packs to the bytes tools/wire_decode.py holds against FrameHeader"
) {
    const PackedByteArray framed
        = netw::wire::frame_pack(300, 3, 19, PackedByteArray(), String());

    REQUIRE(framed.size() == 5);
    NETW_CHECK_EQ(framed[0], 0xac);
    NETW_CHECK_EQ(framed[1], 0x02);
    NETW_CHECK_EQ(framed[2], 0x03);
    NETW_CHECK_EQ(framed[3], 0x13);
    NETW_CHECK_EQ(framed[4], 0x00);
}

} // namespace TestNetwWireFrame
