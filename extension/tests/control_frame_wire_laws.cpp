#include "support/netw_test.h"

#include "netw/session/frames.hpp"

namespace TestControlFrameWire {

using namespace godot;
using netw::session::ControlApply;

TEST_CASE(
    "[Networked][Control][Hosted] KW1 the apply transcribed from WIRE.md 9.9 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    ControlApply applied;
    applied.controller = 2;
    const PackedByteArray bytes = netw::session::frame_write(applied);

    REQUIRE(bytes.size() == 1);
    NETW_CHECK_EQ(bytes[0], 0x02);
}

TEST_CASE(
    "[Networked][Control][Hosted] KW2 a control apply carrying no payload at "
    "all is refused rather than read as controller zero, because an empty "
    "frame and a frame declaring the route uncontrolled say different things"
) {
    ControlApply empty;
    empty.controller = 7;
    const bool refused = !netw::session::frame_read(PackedByteArray(), empty);
    CHECK(refused);

    ControlApply uncontrolled;
    uncontrolled.controller = 0;
    ControlApply read;
    read.controller = 7;
    const bool admitted = netw::session::frame_read(
        netw::session::frame_write(uncontrolled),
        read
    );
    CHECK(admitted);
    NETW_CHECK_EQ(int64_t(read.controller), int64_t(0));
}

} // namespace TestControlFrameWire
