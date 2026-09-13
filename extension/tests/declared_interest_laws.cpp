#include "netw/api/prediction_handle.hpp"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/script.hpp"

namespace TestDeclaredInterestLaws {

using namespace godot;
using namespace netw_test;

Ref<netw::NetwEntity> entity_of(Node *p_node) {
    return netw::NetwEntity::of(p_node);
}

TEST_CASE("[Networked][Interest] L-MIRROR binds and stamps a declared mirror") {
    LoopbackRig rig(1);
    const EntityDecl server_decl = EntityDecl().named("Player").on_route(73);
    rig.declare_entity(server_decl);
    const RID mirror = rig.declare_mirror(0, server_decl);
    Node *node = rig.node_of(mirror, 0);
    Ref<netw::NetwEntity> entity = entity_of(node);

    REQUIRE(node != nullptr);
    REQUIRE(entity.is_valid());
    Object *stamped = entity->get("multiplayer");
    REQUIRE(stamped != nullptr);
    CHECK(netw_test::LoopbackRig::core_of(stamped) == rig.client(0));
    NETW_CHECK_EQ(int(entity->get("route")), 73);
}

TEST_CASE(
    "[Networked][Interest] L-ROLE resolves control from the stamped session"
) {
    LoopbackRig rig(1);
    WorldDecl world;
    world.player("Player", 0);
    rig.declare_world(world);

    Ref<netw::NetwEntity> entity
        = entity_of(rig.node_of(rig.entity_of("Player", 0), 0));
    REQUIRE(entity.is_valid());
    NETW_CHECK_EQ(int(entity->get("controller")), rig.peer_id(0));
    CHECK(bool(entity->get("is_controlled_locally")));
}

TEST_CASE("[Networked][Interest] L-WIRE mirrors one named state shape") {
    LoopbackRig rig(1);
    const EntityDecl decl = EntityDecl()
                                .named("Tracked")
                                .on_route(83)
                                .on_schema("TrackedPose")
                                .synced("position")
                                .placed_at(Vector2(3.0, 4.0));
    rig.declare_entity(decl);
    rig.declare_mirror(0, decl);

    const RID server_set = rig.state_set_of("Tracked");
    const RID client_set = rig.state_set_of("Tracked", 0);
    REQUIRE(server_set.is_valid());
    REQUIRE(client_set.is_valid());
    NETW_CHECK_EQ(
        int(rig.server()->property_set_get_wire_hash(server_set)),
        int(rig.client(0)->property_set_get_wire_hash(client_set))
    );
}

TEST_CASE("[Networked][Interest] L-ACK reads the handle frontier directly") {
    Ref<netw::NetwPredictionHandle> handle;
    handle.instantiate();
    const Ref<netw::NetwPredictStats> stats = handle->get_stats();
    REQUIRE(stats.is_valid());

    stats->set(StringName("ack_confirmed"), 41);

    NETW_CHECK_EQ(handle->get_acknowledged_tick(), 41);
}

} // namespace TestDeclaredInterestLaws

#endif
