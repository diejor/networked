#include "support/netw_test.h"

#include "netw/session/frames.hpp"

namespace TestClockFrameWire {

using namespace godot;
using netw::session::ClockPing;
using netw::session::ClockPong;
using netw::session::ClockRate;

TEST_CASE(
    "[Networked][Clock][Hosted] CW1 the three clock frames transcribed from "
    "WIRE.md 9.7 pack to the bytes tools/wire_decode.py holds them to"
) {
    ClockRate rate;
    rate.tickrate = 60;
    const PackedByteArray rate_bytes = netw::session::frame_write(rate);
    REQUIRE(rate_bytes.size() == 1);
    NETW_CHECK_EQ(rate_bytes[0], 0x3c);

    ClockPing ping;
    ping.origin = 0x12345678;
    const PackedByteArray ping_bytes = netw::session::frame_write(ping);
    REQUIRE(ping_bytes.size() == 4);
    NETW_CHECK_EQ(ping_bytes[0], 0x78);
    NETW_CHECK_EQ(ping_bytes[3], 0x12);

    ClockPong pong;
    pong.origin = 0x12345678;
    pong.tick = 42;
    pong.phase = 128;
    const PackedByteArray pong_bytes = netw::session::frame_write(pong);
    REQUIRE(pong_bytes.size() == 9);
    NETW_CHECK_EQ(pong_bytes[4], 0x2a);
    NETW_CHECK_EQ(pong_bytes[8], 0x80);
}

TEST_CASE(
    "[Networked][Clock][Hosted] CW2 a clock frame carrying one byte the "
    "sender never wrote is refused, because a reply nobody decoded whole "
    "cannot be trusted with the rate the local simulation runs at"
) {
    ClockRate rate;
    rate.tickrate = 60;
    PackedByteArray extended = netw::session::frame_write(rate);
    extended.push_back(0x00);

    ClockRate read;
    const bool refused = !netw::session::frame_read(extended, read);
    CHECK(refused);
}

TEST_CASE(
    "[Networked][Clock][Hosted] CW3 a pong cut short of its phase byte is "
    "refused whole, so no sample is taken from a frame that ended early"
) {
    ClockPong pong;
    pong.origin = 7;
    pong.tick = 9;
    pong.phase = 200;
    const PackedByteArray whole = netw::session::frame_write(pong);
    REQUIRE(whole.size() == 9);

    ClockPong read;
    const bool refused = !netw::session::frame_read(whole.slice(0, 8), read);
    CHECK(refused);

    ClockPong round_trip;
    const bool admitted = netw::session::frame_read(whole, round_trip);
    CHECK(admitted);
    NETW_CHECK_EQ(int64_t(round_trip.phase), int64_t(200));
}

} // namespace TestClockFrameWire
