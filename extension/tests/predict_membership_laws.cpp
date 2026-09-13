#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/predict/engine.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwPredictMembershipLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredictionEngine;
using netw::NetwPredictionHandle;
using netw::NetwPredictIsland;

const char *SEATED_ID = "island_member";

Node *build_member(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Array named(const String &p_name) {
    Array out;
    out.push_back(p_name);
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Ref<NetwEntity> seat(LoopbackRig &p_rig, Node *p_parent, const String &p_name) {
    const int route = p_rig.spawn_registered(
        StringName(SEATED_ID),
        callable_mp_static(&build_member),
        named(p_name),
        one_type(),
        p_parent
    );
    Node *node = p_rig.route_node(route, -1);
    REQUIRE_MESSAGE(node != nullptr, "the spawn reached no node");
    return NetwEntity::of(node);
}

Ref<NetwPredictIsland> island_naming(const Ref<NetwEntity> &p_member) {
    Ref<NetwPredictIsland> rule;
    rule.instantiate();
    rule->add(p_member);
    return rule;
}

void nothing() {
}

TEST_CASE(
    "[Networked][Predict][Membership] IM1 the FIRST roster a slot "
    "commits is a declaration rather than a change, so it opens no "
    "out-of-domain window, and the next roster that MOVES does"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Owner");
    const Ref<NetwEntity> member = seat(rig, arena, "Member");
    REQUIRE(owner.is_valid());
    REQUIRE(member.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);
    pool->set_latest_input_tick(slot, 40);

    const Ref<NetwPredictIsland> declared = island_naming(member);
    pool->refresh_island_membership(
        slot,
        declared,
        false,
        callable_mp_static(&nothing)
    );

    NETW_CHECK_EQ(
        pool->roster_list(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS)
            .size(),
        1
    );
    NETW_CHECK_EQ(pool->out_of_domain_until(slot), -1);

    const Ref<NetwPredictIsland> emptied = island_naming(owner);
    pool->refresh_island_membership(
        slot,
        emptied,
        false,
        callable_mp_static(&nothing)
    );

    NETW_CHECK_EQ(
        pool->roster_list(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS)
            .size(),
        0
    );
    NETW_CHECK_EQ(pool->out_of_domain_until(slot) > 40, 1);
}

TEST_CASE(
    "[Networked][Predict][Membership] IM2 a slot whose island "
    "declares nothing keeps no roster at all, because there is no group for "
    "a member to be seated in"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Lone");
    const Ref<NetwEntity> member = seat(rig, arena, "Other");

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);

    pool->refresh_island_membership(
        slot,
        island_naming(member),
        false,
        callable_mp_static(&nothing)
    );
    REQUIRE(
        pool->roster_list(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS)
            .size()
        == 1
    );

    Ref<NetwPredictIsland> undeclared;
    undeclared.instantiate();
    REQUIRE_FALSE(undeclared->get_declared());
    pool->refresh_island_membership(
        slot,
        undeclared,
        false,
        callable_mp_static(&nothing)
    );

    NETW_CHECK_EQ(
        pool->roster_list(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS)
            .size(),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Membership] IM3 the published roster is "
    "sorted by entity id, so two peers that reached one roster by different "
    "routes report the same rows"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Zulu");
    const Ref<NetwEntity> first = seat(rig, arena, "Yankee");
    const Ref<NetwEntity> second = seat(rig, arena, "Alfa");

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);

    const bool first_is_lower
        = String(first->get_entity_id()) < String(second->get_entity_id());
    TypedArray<NetwEntity> descending;
    descending.push_back(first_is_lower ? second : first);
    descending.push_back(first_is_lower ? first : second);
    pool->roster_assign(
        slot,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        descending
    );

    pool->publish_island_roster(slot);

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(owner->get_prediction())
    );
    REQUIRE(handle != nullptr);
    const PackedStringArray published
        = handle->get_stats()->get(StringName("island_members"));
    REQUIRE(published.size() == 2);
    NETW_CHECK_EQ(String(published[0]) < String(published[1]), 1);
}

} // namespace TestNetwPredictMembershipLaws

#endif
