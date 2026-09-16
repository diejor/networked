#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/clock_config.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwClockCoordinatorLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwClockConfig;
using netw::NetwMultiplayer;

constexpr int COORDINATOR = 7;
constexpr int MEMBER = 9;
constexpr int TICKRATE = 30;
constexpr int HOST_LEAD_TICKS = 25;

void arm_clock(NetwMultiplayer *p_session, bool p_manual) {
    REQUIRE(p_session != nullptr);
    Ref<NetwClockConfig> config;
    config.instantiate();
    config->set("tickrate", TICKRATE);
    NETW_CHECK_EQ(int(p_session->clock_initialize(config)), int(OK));
    p_session->clock_engine().set_manual_tick(p_manual);
}

void deliver(MeshStand &p_stand, NetwMultiplayer *p_a, NetwMultiplayer *p_b) {
    for (int round = 0; round < 8; ++round) {
        p_a->carrier_flush();
        p_b->carrier_flush();
        p_stand.pump(1);
    }
}

TEST_CASE(
    "[Networked][Clock][SceneTree] CC1 a member asks the coordinator for the "
    "tickrate, pings it and calibrates on its pong, so a session whose "
    "coordinator is peer 7 synchronizes to peer 7's tick rather than waiting "
    "on whichever peer holds the transport server seat"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(MEMBER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(host->is_host()), 1);
    NETW_CHECK_EQ(int(guest->is_host()), 0);
    NETW_CHECK_EQ(int(guest->is_server()), 0);

    arm_clock(host, true);
    arm_clock(guest, false);
    host->clock_engine().force_step(HOST_LEAD_TICKS);

    NETW_CHECK_EQ(int(host->clock_engine().get_tick()), HOST_LEAD_TICKS);
    NETW_CHECK_EQ(int(guest->clock_engine().get_tick()), 0);
    NETW_CHECK_EQ(int(guest->clock_is_synchronized()), 0);

    guest->clock_request_handshake();
    deliver(stand, host, guest);

    NETW_CHECK_EQ(int(guest->clock_is_synchronized()), 1);
    NETW_CHECK_EQ(
        int(guest->clock_engine().get_tick()),
        HOST_LEAD_TICKS + int(guest->clock_engine().get_lead_ticks())
    );
}

} // namespace TestNetwClockCoordinatorLaws

#endif
