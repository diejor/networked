#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/interest/decl.hpp"
#include "netw/liveness_core.hpp"
#include "support/netw_call_log.h"

#include <godot_cpp/classes/node2d.hpp>

namespace TestHideVerbLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwLivenessCore;
using netw::NetwMultiplayer;

const char *SHOWN_ID = "shown_body";

Node *build_shown_body(const Variant &p_name) {
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

int a_body_the_client_holds(LoopbackRig &p_rig, Node *p_parent) {
    p_rig.register_constructor(
        p_rig.server(),
        StringName(SHOWN_ID),
        callable_mp_static(&build_shown_body),
        one_type()
    );
    for (int at = 0; at < p_rig.count(); ++at) {
        p_rig.register_constructor(
            p_rig.client(at),
            StringName(SHOWN_ID),
            callable_mp_static(&build_shown_body),
            one_type()
        );
    }
    const int route = p_rig.spawn_registered(
        StringName(SHOWN_ID),
        callable_mp_static(&build_shown_body),
        named("Shown"),
        one_type(),
        p_parent,
        Variant(),
        false
    );
    p_rig.pump(10);
    REQUIRE(p_rig.route_node(route, 0) != nullptr);
    return route;
}

TEST_CASE(
    "[Networked][Liveness] HV1 the hide verb takes a held route to "
    "absent, drops it from the live index, and leaves it naming its entity"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const int route = a_body_the_client_holds(rig, arena);

    NetwMultiplayer *held = rig.client(0);
    CallLog log;
    held->connect(StringName("entity_hidden"), log.callable("hidden"));
    held->connect(StringName("entity_dead"), log.callable("dead"));

    const RID entity = held->entity_from_route(route);
    REQUIRE(entity.is_valid());
    CHECK(held->liveness_hide(route));

    NETW_CHECK_EQ(
        int64_t(held->liveness_route_state(route)),
        int64_t(NetwMultiplayer::ENTITY_STATE_ABSENT)
    );
    NETW_CHECK_EQ(log.count("hidden"), 1);
    NETW_CHECK_EQ(log.count("dead"), 0);
    NETW_CHECK_EQ(int64_t(log.args("hidden")[0]), int64_t(route));

    SUBCASE("the route still names the entity it named while it was here") {
        CHECK(held->entity_from_route(route) == entity);
    }

    SUBCASE("the wrapper is out of the live index") {
        CHECK(held->wrapper_for_route(route).is_null());
    }

    SUBCASE("the server never heard of it, because presence is per peer") {
        NETW_CHECK_EQ(
            int64_t(rig.server()->liveness_route_state(route)),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
    }
}

TEST_CASE(
    "[Networked][Liveness] HV2 a hide that names no held route is "
    "refused and tells nobody"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const int route = a_body_the_client_holds(rig, arena);

    NetwMultiplayer *held = rig.client(0);
    CallLog log;
    held->connect(StringName("entity_hidden"), log.callable("hidden"));

    SUBCASE("a route this peer never heard of") {
        CHECK_FALSE(held->liveness_hide(route + 1000));
        NETW_CHECK_EQ(log.count("hidden"), 0);
    }

    SUBCASE("a route already absent") {
        CHECK(held->liveness_hide(route));
        CHECK_FALSE(held->liveness_hide(route));
        NETW_CHECK_EQ(log.count("hidden"), 1);
    }
}

TEST_CASE(
    "[Networked][Liveness] HV3 a hidden node is told it is hidden and "
    "never that it is ending, and its stage does not move"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const int route = a_body_the_client_holds(rig, arena);

    const Ref<NetwEntity> entity = NetwEntity::of(rig.route_node(route, 0));
    REQUIRE(entity.is_valid());
    const int64_t stage = entity->get_stage();

    CallLog log;
    entity->connect(StringName("hidden"), log.callable("hidden"));
    entity->connect(StringName("despawning"), log.callable("despawning"));
    entity->connect(StringName("despawned"), log.callable("despawned"));

    entity->_remote_hide();

    NETW_CHECK_EQ(log.count("hidden"), 1);
    NETW_CHECK_EQ(log.count("despawning"), 0);
    NETW_CHECK_EQ(log.count("despawned"), 0);
    NETW_CHECK_EQ(entity->get_stage(), stage);
}

TEST_CASE(
    "[Networked][Liveness] HV4 a hidden body keeps the prediction "
    "declaration its game made, because a declaration is scoped to the "
    "identity and a hide loses only the body"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const int route = a_body_the_client_holds(rig, arena);

    const Ref<NetwEntity> entity = NetwEntity::of(rig.route_node(route, 0));
    REQUIRE(entity.is_valid());
    NetwMultiplayer *held = rig.client(0);
    NETW_CHECK_EQ(int(held->lagcomp_initialize(8, 12)), int(godot::OK));
    NETW_CHECK_EQ(
        int(held->predict_declare(entity->get_rid_handle())),
        int(godot::OK)
    );
    const Ref<netw::NetwPredictionHandle> handle = entity->get_prediction();
    REQUIRE(handle.is_valid());
    REQUIRE(handle->is_registered());

    entity->_remote_hide();

    CHECK(handle->is_registered());
}

} // namespace TestHideVerbLaws

#endif
