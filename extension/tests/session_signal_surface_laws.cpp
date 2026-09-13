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
    "[Networked][Session][Hosted] L1 the session announces every edge a game "
    "or a prediction engine listens for, so no listener binds to nothing"
) {
    Ref<NetwMultiplayer> session = make_session();

    SUBCASE("the prediction edges") {
        CHECK(announces(session, "predict_owner_divergence"));
        CHECK(announces(session, "lagcomp_action_gate_fallback"));
    }

    SUBCASE("the scene machine's own edges") {
        CHECK(announces(session, "scene_spawned"));
        CHECK(announces(session, "scene_activated"));
        CHECK(announces(session, "scene_despawned"));
        CHECK(announces(session, "scene_entity_moved"));
        CHECK(announces(session, "scene_startup_spawned"));
    }

    SUBCASE("the tick loop edges that were already native") {
        CHECK(announces(session, "clock_before_tick"));
        CHECK(announces(session, "clock_on_tick"));
        CHECK(announces(session, "clock_after_tick"));
    }

    SUBCASE("an edge nobody declared is not announced") {
        CHECK_FALSE(announces(session, "no_such_edge"));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a divergence edge carries the peer, the "
    "entry and the attribution, so a listener never has to guess a shape"
) {
    Ref<NetwMultiplayer> session = make_session();

    int arity = -1;
#if defined(NETW_MODULE)
    List<MethodInfo> held;
    session->get_signal_list(&held);
    for (const MethodInfo &row : held) {
        if (row.name == StringName("predict_owner_divergence")) {
            arity = row.arguments.size();
        }
    }
#else
    const TypedArray<Dictionary> held = session->get_signal_list();
    for (int at = 0; at < held.size(); at++) {
        const Dictionary row = held[at];
        const String row_name = String(row[StringName("name")]);
        if (row_name == String("predict_owner_divergence")) {
            arity = int(Array(row[StringName("args")]).size());
        }
    }
#endif
    NETW_CHECK_EQ(arity, 3);
}

} // namespace TestNetwSessionSignalSurface
