#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"

namespace TestSpawnSettleLaws {

using namespace godot;
using namespace netw_test;
using netw::EventPlane;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;

Node *build_body(const Variant &) {
    Node *made = memnew(Node);
    made->set_name("Body");
    return made;
}

Array one_arg() {
    Array out;
    out.push_back(String("body"));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

int64_t state_of(LoopbackRig &p_rig, int p_client, int p_route) {
    NetwMultiplayer *api
        = p_client < 0 ? p_rig.server() : p_rig.client(p_client);
    return int64_t(api->entity_get_state(api->entity_from_route(p_route)));
}

int64_t stage_rows(NetwMultiplayer *p_api, int p_route, int64_t p_event) {
    if (p_api == nullptr) {
        return 0;
    }
    const Array ring = p_api->event_ring(p_route);
    int64_t seen = 0;
    for (int at = 0; at < ring.size(); ++at) {
        const Dictionary row = ring[at];
        if (!row.is_empty()
            && int64_t(row[netw::event_key::event()]) == p_event) {
            seen += 1;
        }
    }
    return seen;
}

TEST_CASE(
    "[Networked][Spawn] SW1 a root that leaves the tree despawns at the PUMP "
    "with no frame anywhere in the run, because the edge belongs to the pump "
    "the session already drives"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena
    );
    const bool arrived = rig.route_node(route, 0) != nullptr;
    CHECK(arrived);

    Node *host_node = rig.route_node(route);
    arena->remove_child(host_node);
    rig.pump(10);

    const bool seat_lost = rig.route_node(route, 0) == nullptr;
    CHECK(seat_lost);
    NETW_CHECK_EQ(
        state_of(rig, -1, route),
        int64_t(netw::NetwLivenessCore::STATE_DEAD)
    );

    memdelete(host_node);
}

TEST_CASE(
    "[Networked][Spawn] SW2 a visibility loss committed between two pumps "
    "reaches the peer, because only the sweep can revoke an admission the "
    "entity itself never moved out of"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena
    );

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("settle"));
    REQUIRE(layer.is_valid());
    const Ref<NetwEntity> entity = NetwEntity::of(rig.route_node(route));
    REQUIRE(entity.is_valid());

    layer->add_entity(entity);
    rig.flush_interest();
    rig.pump(10);
    const bool hidden = rig.route_node(route, 0) == nullptr;
    CHECK(hidden);

    layer->add_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);
    const bool shown = rig.route_node(route, 0) != nullptr;
    CHECK(shown);

    layer->remove_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);
    const bool revoked = rig.route_node(route, 0) == nullptr;
    CHECK(revoked);
}

TEST_CASE(
    "[Networked][Spawn] SW3 a spawn leaves one row for each stage that ran "
    "it ON THE PEER THAT RAN IT, so an observer can tell which half of a "
    "materialization it is looking at"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    rig.server()->event_arm(true);
    rig.client(0)->event_arm(true);

    const int route = rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena
    );

    NetwMultiplayer *core = rig.server();
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("stages"));
    REQUIRE(layer.is_valid());
    const Ref<NetwEntity> entity = NetwEntity::of(rig.route_node(route));
    REQUIRE(entity.is_valid());

    layer->add_entity(entity);
    rig.flush_interest();
    rig.pump(10);
    layer->add_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);

    const bool arrived = rig.route_node(route, 0) != nullptr;
    CHECK(arrived);

    const int64_t host_declared
        = stage_rows(rig.server(), route, EventPlane::SPAWN_DECLARE);
    const int64_t host_reconciled
        = stage_rows(rig.server(), 0, EventPlane::SPAWN_RECONCILE);
    const int64_t seat_built
        = stage_rows(rig.client(0), 0, EventPlane::SPAWN_CONSTRUCT);
    const int64_t seat_declared
        = stage_rows(rig.client(0), route, EventPlane::SPAWN_DECLARE);

    NETW_CHECK_GT(host_declared, int64_t(0));
    NETW_CHECK_GT(host_reconciled, int64_t(0));
    NETW_CHECK_GT(seat_built, int64_t(0));
    NETW_CHECK_EQ(seat_declared, int64_t(0));
}

TEST_CASE(
    "[Networked][Spawn] SW4 the frame is snapshotted at the first pump AFTER "
    "tree entry, so everything written between the verb and that pump rides "
    "it rather than being lost to a window nobody could see into"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena,
        Variant(),
        false
    );

    Node *host_node = rig.route_node(route);
    const bool placed = host_node != nullptr;
    CHECK(placed);
    if (!placed) {
        return;
    }
    host_node->set_name("Renamed");

    rig.pump(8);

    Node *seat_node = rig.route_node(route, 0);
    const bool arrived = seat_node != nullptr;
    CHECK(arrived);
    if (!arrived) {
        return;
    }
    const bool carried_the_later_write
        = seat_node->get_name() == StringName("Renamed");
    CHECK(carried_the_later_write);
}

TEST_CASE(
    "[Networked][Spawn] SF1 an interest re-admission REVIVES the route it "
    "killed rather than minting a new one, because a route names one entity "
    "for its whole life and a visibility edge is not a lifecycle event"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena,
        Variant(),
        false
    );

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("flap"));
    const Ref<NetwEntity> entity = NetwEntity::of(rig.route_node(route));
    REQUIRE(layer.is_valid());
    REQUIRE(entity.is_valid());

    layer->add_entity(entity);
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, route),
        int64_t(netw::NetwLivenessCore::STATE_UNKNOWN)
    );

    layer->add_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, route),
        int64_t(netw::NetwLivenessCore::STATE_LIVE)
    );
    layer->remove_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, route),
        int64_t(netw::NetwLivenessCore::STATE_ABSENT)
    );
    const bool gone = rig.route_node(route, 0) == nullptr;
    CHECK(gone);

    layer->add_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, route),
        int64_t(netw::NetwLivenessCore::STATE_LIVE)
    );
    const Ref<NetwEntity> revived = NetwEntity::of(rig.route_node(route, 0));
    CHECK(revived.is_valid());
    if (revived.is_valid()) {
        NETW_CHECK_EQ(revived->get_route(), int64_t(route));
    }
}

} // namespace TestSpawnSettleLaws

#endif
