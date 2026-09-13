#include "support/netw_test.h"

#include "netw/session/frames.hpp"

namespace TestDenyFrameWire {

using namespace godot;
using netw::session::DenyKey;

TEST_CASE(
    "[Networked][Lagcomp][Hosted] DW1 the denial transcribed from WIRE.md 9.8 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    DenyKey deny;
    deny.key = StringName("shot");
    const PackedByteArray bytes = netw::session::frame_write(deny);

    REQUIRE(bytes.size() == 6);
    NETW_CHECK_EQ(bytes[0], 0x04);
    NETW_CHECK_EQ(bytes[1], 0x00);
    NETW_CHECK_EQ(bytes[2], uint8_t('s'));
    NETW_CHECK_EQ(bytes[5], uint8_t('t'));
}

TEST_CASE(
    "[Networked][Lagcomp][Hosted] DW2 a denial whose key runs past the end of "
    "its own frame names nothing, because reverting a prediction the server "
    "never spoke about is worse than leaving it for reconciliation"
) {
    DenyKey deny;
    deny.key = StringName("shot");
    const PackedByteArray whole = netw::session::frame_write(deny);

    DenyKey read;
    const bool refused
        = !netw::session::frame_read(whole.slice(0, whole.size() - 1), read);
    CHECK(refused);

    DenyKey round_trip;
    const bool admitted = netw::session::frame_read(whole, round_trip);
    CHECK(admitted);
    const bool named = String(round_trip.key) == String("shot");
    CHECK(named);
}

} // namespace TestDenyFrameWire
