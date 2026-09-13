#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/netw_multiplayer.hpp"
#include "support/loopback_rig.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace TestNetwSessionLifetime {

using namespace godot;

Ref<RefCounted> bare_api() {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(NodePath("/"));
    return netw::NetwMultiplayer::make(inner, Ref<Script>());
}

TEST_CASE(
    "[Networked][Session] SL1 a session built with no peer frees when the "
    "last reference to it drops, because every seam inside it holds it by "
    "ObjectID and nothing it owns points back strongly"
) {
    uint64_t id = 0;
    int held = 0;
    {
        Ref<RefCounted> api = bare_api();
        id = api->get_instance_id();
        held = api->get_reference_count();
    }
    const bool alive = UtilityFunctions::is_instance_id_valid(int64_t(id));
    NETW_CHECK_EQ(held, 1);
    NETW_CHECK_EQ(alive ? 1 : 0, 0);
}

TEST_CASE(
    "[Networked][Session] SL2 a session that carried a peer frees once its "
    "link resets, because a transport holds the session it serves by "
    "ObjectID and a reset link keeps no peer of its own"
) {
    uint64_t id = 0;
    int held = 0;
    {
        Ref<netw::LocalLoopbackSession> link;
        link.instantiate();
        Ref<RefCounted> api = bare_api();
        api->set("multiplayer_peer", link->get_server_peer());
        id = api->get_instance_id();
        held = api->get_reference_count();
        link->reset();
    }
    const bool alive = UtilityFunctions::is_instance_id_valid(int64_t(id));
    NETW_CHECK_EQ(held, 1);
    NETW_CHECK_EQ(alive ? 1 : 0, 0);
}

TEST_CASE(
    "[Networked][Session] SL3 a session that declared a predicted world frees "
    "with that world, because the predict engine holds its session weakly and "
    "a predicted entity is owned by the rig rather than by the session"
) {
    uint64_t id = 0;
    {
        netw_test::LoopbackRig rig(1);
        netw_test::WorldDecl world;
        world.clocked(30, 3).lag_compensated().player(
            netw_test::EntityDecl()
                .named("P")
                .on_schema("PredictedPose")
                .synced("position")
                .placed_at(Vector2())
                .predicted(0.01)
                .scheduled(netw::Schedule::TICK),
            0
        );
        rig.mount();
        rig.declare_world(world);
        id = rig.client(0)->get_instance_id();
    }
    const bool alive = UtilityFunctions::is_instance_id_valid(int64_t(id));
    NETW_CHECK_EQ(alive ? 1 : 0, 0);
}

} // namespace TestNetwSessionLifetime

#endif
