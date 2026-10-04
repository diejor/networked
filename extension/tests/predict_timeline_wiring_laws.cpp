#include "support/loopback_rig.h"
#include "support/predict_rows.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
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
using netw::NetwPrediction;
using netw::NetwPredictionEngine;

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

int64_t rewind_slot(NetwMultiplayer *p_core, const Ref<NetwEntity> &p_entity) {
    NetwLagCompCore *registry = p_core->get_lagcomp_core();
    REQUIRE(registry != nullptr);
    return registry->timeline_slot_of(p_entity);
}

TEST_CASE(
    "[Networked][Predict][Law] TW1 a role that READS authority enrolls "
    "the entity in the rewind registry and takes an input lane of its own, "
    "and a role that SPECULATES takes a private lane and enrolls nothing"
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
    NETW_CHECK_EQ(rewind_slot(core, seated), int64_t(-1));

    CHECK(
        pool->wire_role_timeline(slot, int(NetwPrediction::ROLE_CONSUME), false)
    );
    const int64_t enrolled = rewind_slot(core, seated);
    CHECK(enrolled >= 0);
    REQUIRE(pool->input_lane_open(slot));
    LocalVector<netw::predict::FieldDecl> fire;
    fire.push_back(netw::field_decl(StringName("fire"), 0));
    pool->rewire(slot, LocalVector<netw::predict::FieldDecl>(), fire);
    Dictionary pressed;
    pressed[StringName("fire")] = true;
    pool->record_authored_command(
        slot,
        3,
        3,
        true,
        netw_test::input_row(pool, slot, pressed)
    );
    CHECK(pool->has_authored_input(slot, 3));

    SUBCASE("a host-local role keeps the same enrollment") {
        CHECK(pool->wire_role_timeline(
            slot,
            int(NetwPrediction::ROLE_HOST_LOCAL),
            false
        ));
        NETW_CHECK_EQ(rewind_slot(core, seated), enrolled);
        CHECK(pool->input_lane_open(slot));
    }

    SUBCASE("and a predicting role takes a fresh private lane instead") {
        CHECK(pool->wire_role_timeline(
            slot,
            int(NetwPrediction::ROLE_PREDICT),
            false
        ));
        REQUIRE(pool->input_lane_open(slot));
        CHECK_FALSE(pool->has_authored_input(slot, 3));
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

    CHECK_FALSE(
        pool->wire_role_timeline(slot, int(NetwPrediction::ROLE_REMOTE), false)
    );
    CHECK_FALSE(pool->input_lane_open(slot));

    SUBCASE("but a latched remote speculates, so it does take one") {
        CHECK(pool->wire_role_timeline(
            slot,
            int(NetwPrediction::ROLE_REMOTE),
            true
        ));
        CHECK(pool->input_lane_open(slot));
        NETW_CHECK_EQ(rewind_slot(core, seated), int64_t(-1));
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
    REQUIRE(seated->get_state_binding() != nullptr);
    REQUIRE_MESSAGE(
        seated->get_input_binding() == nullptr,
        "the fixture declares no input, which is what this law is about"
    );

    NetwMultiplayer *core = flow_core(rig.server());
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    NETW_CHECK_EQ(
        int(core->predict_declare(seated->get_rid_handle())),
        int(OK)
    );

    const Ref<netw::NetwPrediction> handle = seated->get_prediction();
    REQUIRE(handle.is_valid());
    NETW_CHECK_EQ(
        handle->get_input_source(),
        int(NetwPrediction::INPUT_SOURCE_NONE)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(NetwPrediction::SIM_MODE_AUTHORITATIVE)
    );
    NETW_CHECK_EQ(
        int(pool->role_of(pool->slot_register(seated))),
        int(NetwPrediction::ROLE_HOST_LOCAL)
    );
}

} // namespace TestNetwPredictTimelineWiringLaws

#endif
