#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/timeline.hpp"
#include "netw/predict/engine.hpp"
#include "netw/prediction_core.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwPredictAnchorLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionEngine;
using netw::NetwPredictionHandle;
using netw::NetwPredictRecovery;
using netw::NetwTimeline;

const char *ANCHORED_ID = "anchored_player";

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
        StringName(ANCHORED_ID),
        callable_mp_static(&build_player),
        named("Anchored"),
        one_type(),
        p_parent
    );
    Node *node = p_rig.route_node(route, -1);
    REQUIRE_MESSAGE(node != nullptr, "the spawn reached no node");
    return NetwEntity::of(node);
}

Dictionary authority_at(double p_x) {
    Dictionary out;
    out[StringName("position")] = Vector2(real_t(p_x), 0.0);
    return out;
}

TEST_CASE(
    "[Networked][Predict][Law] AN1 a corrected tick lane anchors "
    "authority OVER the prediction it replaced, so a duplicate ack compares "
    "against the correction and never re-triggers it"
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

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE_MESSAGE(handle != nullptr, "the entity minted no handle");
    handle->set_schedule(NetwPredict::SCHEDULE_TICK);

    const Ref<NetwTimeline> lane
        = NetwTimeline::create(NetwTimeline::DEFAULT_LIMIT);
    pool->bind_timeline(slot, lane);
    lane->record_state(6, authority_at(1.0));

    const netw::RecoveryPlan declined;
    pool->close_state_recovery(
        slot,
        5,
        authority_at(9.0),
        declined,
        0,
        Dictionary(),
        Dictionary(),
        Dictionary(),
        PackedStringArray(),
        Callable()
    );

    const Dictionary anchored = lane->state_at(6);
    REQUIRE_MESSAGE(
        !anchored.is_empty(),
        "the corrected transition left no anchored state"
    );
    NETW_CHECK_EQ(Vector2(anchored[StringName("position")]).x, real_t(9.0));
    NETW_CHECK_EQ(
        Vector2(lane->latest_state_at_or_before(6)[StringName("position")]).x,
        real_t(9.0)
    );
}

TEST_CASE(
    "[Networked][Predict][Law] AN2 a frame lane anchors nothing, "
    "because a frame transition names no tick for a later ack to key on"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE(handle != nullptr);
    handle->set_schedule(NetwPredict::SCHEDULE_FRAME);

    const Ref<NetwTimeline> lane
        = NetwTimeline::create(NetwTimeline::DEFAULT_LIMIT);
    pool->bind_timeline(slot, lane);

    const netw::RecoveryPlan declined;
    pool->close_state_recovery(
        slot,
        5,
        authority_at(9.0),
        declined,
        0,
        Dictionary(),
        Dictionary(),
        Dictionary(),
        PackedStringArray(),
        Callable()
    );

    CHECK(lane->state_at(6).is_empty());
}

} // namespace TestNetwPredictAnchorLaws

#endif
