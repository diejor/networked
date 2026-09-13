#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/timeline.hpp"
#include "netw/lagcomp_core.hpp"
#include "netw/predict/engine.hpp"
#include "support/value_flow_stand.h"
#include "support/world_decl.h"

#include "support/declared_nodes.h"
#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwPredictTimelineWiringLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwLagCompCore;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionEngine;
using netw::NetwTimeline;

const char *WIRED_ID = "wired_player";

Node *build_player(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Ref<NetwEntity> seat_player(LoopbackRig &p_rig, Node *p_parent) {
    const int route = p_rig.spawn_registered(
        StringName(WIRED_ID),
        callable_mp_static(&build_player),
        named("Wired"),
        one_type(),
        p_parent
    );
    Node *node = p_rig.route_node(route, -1);
    REQUIRE_MESSAGE(node != nullptr, "the spawn reached no node");
    return NetwEntity::of(node);
}

Ref<NetwTimeline> registry_history(
    NetwMultiplayer *p_core,
    const Ref<NetwEntity> &p_entity
) {
    NetwLagCompCore *registry = p_core->get_lagcomp_core();
    REQUIRE(registry != nullptr);
    const int64_t seated
        = registry->timeline_register(p_entity, NetwTimeline::DEFAULT_LIMIT);
    REQUIRE(seated >= 0);
    return registry->timeline_history(seated);
}

TEST_CASE(
    "[Networked][Predict][Law] TW1 a role that READS authority is "
    "wired to the registry's own history, and a role that SPECULATES gets a "
    "private one, so a consumer and the rewind server never disagree about "
    "what authority recorded"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    const Ref<NetwTimeline> registered = registry_history(core, seated);
    REQUIRE(registered.is_valid());

    const Ref<NetwTimeline> declared
        = NetwTimeline::create(NetwTimeline::DEFAULT_LIMIT);
    CHECK(pool->wire_role_timeline(
        slot,
        int(NetwPredict::ROLE_CONSUME),
        false,
        declared
    ));
    CHECK(pool->timeline_of(slot) == registered);
    CHECK(pool->timeline_of(slot) != declared);

    SUBCASE("a host-local role reads the same registered history") {
        CHECK(pool->wire_role_timeline(
            slot,
            int(NetwPredict::ROLE_HOST_LOCAL),
            false,
            declared
        ));
        CHECK(pool->timeline_of(slot) == registered);
    }

    SUBCASE("and a predicting role takes a private history instead") {
        CHECK(pool->wire_role_timeline(
            slot,
            int(NetwPredict::ROLE_PREDICT),
            false,
            declared
        ));
        const Ref<NetwTimeline> own = pool->timeline_of(slot);
        REQUIRE(own.is_valid());
        CHECK(own != registered);
        CHECK(own != declared);
        CHECK(seated->get_timeline() == own);
    }
}

TEST_CASE(
    "[Networked][Predict][Law] TW2 a role that neither speculates "
    "nor reads authority is wired to nothing, so a latched remote keeps the "
    "history it already had"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    const Ref<NetwTimeline> declared
        = NetwTimeline::create(NetwTimeline::DEFAULT_LIMIT);
    CHECK_FALSE(pool->wire_role_timeline(
        slot,
        int(NetwPredict::ROLE_REMOTE),
        false,
        declared
    ));

    SUBCASE("but a latched remote speculates, so it does take one") {
        CHECK(pool->wire_role_timeline(
            slot,
            int(NetwPredict::ROLE_REMOTE),
            true,
            declared
        ));
        const Ref<NetwTimeline> own = pool->timeline_of(slot);
        REQUIRE(own.is_valid());
        CHECK(own != declared);
        CHECK(own != registry_history(core, seated));
    }
}

TEST_CASE(
    "[Networked][Predict][Law] TW3 a body carrying state and no command set "
    "still wires, so a server-controlled entity nobody steers is driven "
    "rather than seated as a display proxy on the peer that owns it"
) {
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30).lag_compensated());
    rig.mount();
    const FlowPair pair = stand_flow_pair(
        rig,
        netw_test::gdsrc::STATE_VOLATILE_AND_RETAINED,
        "Coasting",
        BIND_NO_MIRROR
    );
    const Ref<NetwEntity> seated = NetwEntity::of(pair.authored);
    REQUIRE(seated.is_valid());
    REQUIRE(seated->get_state_binding().is_valid());
    REQUIRE_MESSAGE(
        seated->get_input_binding().is_null(),
        "the fixture declares no input, which is what this law is about"
    );

    NetwMultiplayer *core = flow_core(rig.server());
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    NETW_CHECK_EQ(
        int(core->predict_declare(seated->get_rid_handle())),
        int(OK)
    );

    const Ref<netw::NetwPredictionHandle> handle = seated->get_prediction();
    REQUIRE(handle.is_valid());
    NETW_CHECK_EQ(
        handle->get_input_source(),
        int(NetwPredict::INPUT_SOURCE_NONE)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(NetwPredict::SIM_MODE_AUTHORITATIVE)
    );
    NETW_CHECK_EQ(
        int(pool->role_of(pool->slot_register(seated))),
        int(NetwPredict::ROLE_HOST_LOCAL)
    );
}

} // namespace TestNetwPredictTimelineWiringLaws

#endif
