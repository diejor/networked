#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionSignalSurface {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

bool announces(const Ref<NetwMultiplayer> &p_session, const char *p_name) {
    return p_session->has_signal(StringName(p_name));
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 the session declares no signal for an "
    "edge it notifies, so observe is the only way to hear one"
) {
    Ref<NetwMultiplayer> session = make_session();

    SUBCASE("the prediction edges") {
        CHECK_FALSE(announces(session, "predict_owner_divergence"));
        CHECK_FALSE(announces(session, "lagcomp_action_gate_fallback"));
    }

    SUBCASE("the scene machine's own edges") {
        CHECK_FALSE(announces(session, "scene_spawned"));
        CHECK_FALSE(announces(session, "scene_activated"));
        CHECK_FALSE(announces(session, "scene_despawned"));
        CHECK_FALSE(announces(session, "scene_entity_moved"));
        CHECK_FALSE(announces(session, "scene_startup_spawned"));
    }

    SUBCASE("the tick loop edges") {
        CHECK_FALSE(announces(session, "clock_before_tick"));
        CHECK_FALSE(announces(session, "clock_on_tick"));
        CHECK_FALSE(announces(session, "clock_after_tick"));
    }

    SUBCASE("an edge nobody declared is not announced") {
        CHECK_FALSE(announces(session, "no_such_edge"));
    }
}

} // namespace TestNetwSessionSignalSurface
