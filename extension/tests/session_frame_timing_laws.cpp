#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/engine.hpp"

namespace TestNetwSessionFrameTiming {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> clocked(int p_rate) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->clock_engine().set_tickrate(p_rate);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 the frame timing a driver reads carries "
    "the tick just completed, which is one behind the clock's own"
) {
    Ref<NetwMultiplayer> session = clocked(8);

    const netw::NetwPredictTiming timing = session->frame_timing();
    NETW_CHECK_EQ(timing.get_tick(), session->clock_get_tick() - 1);

    SUBCASE("the delta and the ticktime are the same fixed step") {
        NETW_CHECK_CLOSE(
            timing.get_delta(),
            session->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_TICKTIME),
            0.0001
        );
        NETW_CHECK_CLOSE(timing.get_delta(), timing.get_ticktime(), 0.0001);
    }

    SUBCASE("the quantum is at least one, so a driver never steps zero") {
        NETW_CHECK_GE(timing.get_quantum(), 1);
        NETW_CHECK_GE(session->declared_quantum(), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 the simulated frame a timing carries "
    "counts the solves that ran, and a tick drive reads the same count as a "
    "frame drive"
) {
    Ref<NetwMultiplayer> session = clocked(8);

    const int64_t opened = session->frame_timing().get_frame();
    session->physics_frame_advance();
    session->physics_frame_advance();
    NETW_CHECK_EQ(session->frame_timing().get_frame(), opened + 2);
    NETW_CHECK_EQ(session->tick_timing(4, 0.125).get_frame(), opened + 2);

    SUBCASE("a frame the clock held is not a solve, so it costs nothing") {
        session->clock_set_gate(true);
        session->clock_step(0);
        REQUIRE_FALSE(session->clock_is_simulating());
        const int64_t held = session->frame_timing().get_frame();
        session->physics_frame_advance();
        NETW_CHECK_EQ(session->frame_timing().get_frame(), held);
    }

    SUBCASE("a tick drive carries the label and step it was handed") {
        const netw::NetwPredictTiming driven = session->tick_timing(4, 0.125);
        NETW_CHECK_EQ(driven.get_tick(), 4);
        NETW_CHECK_CLOSE(driven.get_delta(), 0.125, 0.0001);
        NETW_CHECK_CLOSE(
            driven.get_ticktime(),
            session->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_TICKTIME),
            0.0001
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 an entity that presents no node belongs "
    "to no physics space, and says so with dimension zero"
) {
    Ref<NetwMultiplayer> session = clocked(8);
    const RID entity = session->entity_create();

    const Dictionary held = session->entity_space(entity);
    NETW_CHECK_EQ(int(held[StringName("dimension")]), 0);
    CHECK_FALSE(RID(held[StringName("space")]).is_valid());

    SUBCASE("and an entity nobody minted answers the same shape") {
        const Dictionary none = session->entity_space(RID());
        NETW_CHECK_EQ(int(none[StringName("dimension")]), 0);
    }
}

} // namespace TestNetwSessionFrameTiming
