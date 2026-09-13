#include "support/netw_test.h"

#include "netw/wire/frame.hpp"

using namespace godot;

namespace TestNetwWireFrameUnpack {

using godot::PackedByteArray;
using godot::String;
using netw::wire::FRAME_COMP_PATH;
using netw::wire::frame_pack;
using netw::wire::frame_unpack_all;
using netw::wire::FrameWalk;

PackedByteArray bytes(const std::initializer_list<uint8_t> &p_values) {
    PackedByteArray out;
    for (const uint8_t value : p_values) {
        out.push_back(value);
    }
    return out;
}

PackedByteArray joined(
    const PackedByteArray &p_first,
    const PackedByteArray &p_second
) {
    PackedByteArray out = p_first;
    out.append_array(p_second);
    return out;
}

PackedByteArray corrupt_varint() {
    return bytes({0xff, 0xff, 0xff, 0xff, 0xff});
}

TEST_CASE(
    "[Networked][Wire][Hosted] U1 a packed frame walks back out as the route, "
    "comp, channel and payload it was packed from"
) {
    const FrameWalk walk = frame_unpack_all(
        frame_pack(300, 3, 19, bytes({1, 2, 3}), String()),
        0
    );

    NETW_CHECK_EQ(walk.whole, true);
    REQUIRE(walk.frames.size() == 1);
    NETW_CHECK_EQ(walk.frames[0].route, 300);
    NETW_CHECK_EQ(walk.frames[0].comp, 3);
    NETW_CHECK_EQ(walk.frames[0].channel, 19);
    NETW_CHECK_EQ(walk.frames[0].path.is_empty(), true);
    NETW_CHECK_EQ(walk.frames[0].payload.size(), 3);
    NETW_CHECK_EQ(walk.frames[0].payload[0], 1);
    NETW_CHECK_EQ(walk.frames[0].payload[2], 3);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U2 a comp 255 frame gives its path back beside "
    "its payload rather than folded into it"
) {
    const FrameWalk walk = frame_unpack_all(
        frame_pack(
            2,
            FRAME_COMP_PATH,
            5,
            bytes({7, 8}),
            String("res://moved.tscn")
        ),
        0
    );

    NETW_CHECK_EQ(walk.whole, true);
    REQUIRE(walk.frames.size() == 1);
    NETW_CHECK_EQ(walk.frames[0].comp, FRAME_COMP_PATH);
    NETW_CHECK_EQ(walk.frames[0].path == String("res://moved.tscn"), true);
    NETW_CHECK_EQ(walk.frames[0].payload.size(), 2);
    NETW_CHECK_EQ(walk.frames[0].payload[0], 7);
    NETW_CHECK_EQ(walk.frames[0].payload[1], 8);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U3 a comp 255 frame with an empty path keeps "
    "its whole payload, because the zero length prefix is still consumed"
) {
    const FrameWalk walk = frame_unpack_all(
        frame_pack(1, FRAME_COMP_PATH, 2, bytes({5, 6}), String()),
        0
    );

    NETW_CHECK_EQ(walk.whole, true);
    REQUIRE(walk.frames.size() == 1);
    NETW_CHECK_EQ(walk.frames[0].path.is_empty(), true);
    NETW_CHECK_EQ(walk.frames[0].payload.size(), 2);
    NETW_CHECK_EQ(walk.frames[0].payload[0], 5);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U4 frames packed back to back walk out in "
    "arrival order, each with its own route and payload"
) {
    PackedByteArray framed = frame_pack(1, 0, 11, bytes({0xa1}), String());
    framed
        = joined(framed, frame_pack(300, 3, 12, bytes({0xb1, 0xb2}), String()));
    framed = joined(
        framed,
        frame_pack(5, FRAME_COMP_PATH, 13, bytes({0xc1}), String("ab"))
    );

    const FrameWalk walk = frame_unpack_all(framed, 0);

    NETW_CHECK_EQ(walk.whole, true);
    REQUIRE(walk.frames.size() == 3);
    NETW_CHECK_EQ(walk.frames[0].route, 1);
    NETW_CHECK_EQ(walk.frames[0].channel, 11);
    NETW_CHECK_EQ(walk.frames[0].payload[0], 0xa1);
    NETW_CHECK_EQ(walk.frames[1].route, 300);
    NETW_CHECK_EQ(walk.frames[1].channel, 12);
    NETW_CHECK_EQ(walk.frames[1].payload.size(), 2);
    NETW_CHECK_EQ(walk.frames[1].payload[1], 0xb2);
    NETW_CHECK_EQ(walk.frames[2].route, 5);
    NETW_CHECK_EQ(walk.frames[2].channel, 13);
    NETW_CHECK_EQ(walk.frames[2].path == String("ab"), true);
    NETW_CHECK_EQ(walk.frames[2].payload[0], 0xc1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U5 a walk starts where it is told, so a "
    "datagram's carrier header is stepped over rather than parsed as a frame"
) {
    const PackedByteArray framed = joined(
        bytes({0x99, 0x98, 0x97, 0x96}),
        frame_pack(9, 0, 21, bytes({4, 4}), String())
    );

    const FrameWalk walk = frame_unpack_all(framed, 4);

    NETW_CHECK_EQ(walk.whole, true);
    REQUIRE(walk.frames.size() == 1);
    NETW_CHECK_EQ(walk.frames[0].route, 9);
    NETW_CHECK_EQ(walk.frames[0].channel, 21);
    NETW_CHECK_EQ(walk.frames[0].payload.size(), 2);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U6 an empty span is a whole walk of no frames, "
    "so a datagram that is all header says nothing rather than failing"
) {
    const PackedByteArray framed = frame_pack(9, 0, 21, bytes({4}), String());

    const FrameWalk empty = frame_unpack_all(PackedByteArray(), 0);
    NETW_CHECK_EQ(empty.whole, true);
    NETW_CHECK_EQ(empty.frames.size(), 0);

    const FrameWalk consumed = frame_unpack_all(framed, framed.size());
    NETW_CHECK_EQ(consumed.whole, true);
    NETW_CHECK_EQ(consumed.frames.size(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U7 a route varint that never terminates stops "
    "the walk with nothing read, because the split point is unknowable"
) {
    const FrameWalk walk = frame_unpack_all(corrupt_varint(), 0);

    NETW_CHECK_EQ(walk.whole, false);
    NETW_CHECK_EQ(walk.frames.size(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U8 a body length varint that never terminates "
    "stops the walk, and the whole frames read before it survive"
) {
    const PackedByteArray framed = joined(
        frame_pack(1, 0, 7, bytes({9}), String()),
        joined(bytes({2, 0, 8}), corrupt_varint())
    );

    const FrameWalk walk = frame_unpack_all(framed, 0);

    NETW_CHECK_EQ(walk.whole, false);
    REQUIRE(walk.frames.size() == 1);
    NETW_CHECK_EQ(walk.frames[0].route, 1);
    NETW_CHECK_EQ(walk.frames[0].channel, 7);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U9 a path that overruns its own body loses the "
    "frame, because a path read past the body is another frame's bytes"
) {
    const PackedByteArray framed
        = joined(bytes({1, FRAME_COMP_PATH, 3, 5}), corrupt_varint());

    const FrameWalk walk = frame_unpack_all(framed, 0);

    NETW_CHECK_EQ(walk.whole, false);
    NETW_CHECK_EQ(walk.frames.size(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U10 a header that stops mid frame yields no "
    "frame, because every field after the cut would be read as a zero"
) {
    const FrameWalk walk = frame_unpack_all(bytes({0x07}), 0);

    NETW_CHECK_EQ(walk.whole, false);
    NETW_CHECK_EQ(walk.frames.size(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U11 a body length that overruns the buffer "
    "yields no frame, rather than a frame holding the bytes that are there"
) {
    const PackedByteArray framed
        = frame_pack(1, 0, 0, bytes({1, 2, 3, 4}), String());

    const FrameWalk walk = frame_unpack_all(framed.slice(0, 6), 0);

    NETW_CHECK_EQ(walk.whole, false);
    NETW_CHECK_EQ(walk.frames.size(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U12 a trailing partial frame leaves the whole "
    "frames before it intact and marks the walk unwhole"
) {
    const PackedByteArray framed = joined(
        joined(
            frame_pack(1, 0, 7, bytes({0xa1}), String()),
            frame_pack(2, 0, 8, bytes({0xb1}), String())
        ),
        bytes({0x07})
    );

    const FrameWalk walk = frame_unpack_all(framed, 0);

    NETW_CHECK_EQ(walk.whole, false);
    REQUIRE(walk.frames.size() == 2);
    NETW_CHECK_EQ(walk.frames[0].route, 1);
    NETW_CHECK_EQ(walk.frames[0].payload[0], 0xa1);
    NETW_CHECK_EQ(walk.frames[1].route, 2);
    NETW_CHECK_EQ(walk.frames[1].payload[0], 0xb1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] U13 a route spelled with a redundant final "
    "group is refused, so one route has exactly one spelling on the wire"
) {
    const FrameWalk canonical
        = frame_unpack_all(bytes({0xac, 0x02, 0x00, 0x13, 0x00}), 0);

    NETW_CHECK_EQ(canonical.whole, true);
    REQUIRE(canonical.frames.size() == 1);
    NETW_CHECK_EQ(canonical.frames[0].route, 300);

    const FrameWalk redundant
        = frame_unpack_all(bytes({0xac, 0x82, 0x00, 0x00, 0x13, 0x00}), 0);

    NETW_CHECK_EQ(redundant.whole, false);
    NETW_CHECK_EQ(redundant.frames.size(), 0);
}

} // namespace TestNetwWireFrameUnpack
