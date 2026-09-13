#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneBoundaryReadLaws {

using namespace godot;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct BoundScene {
    Node *root = nullptr;
    Ref<netw::NetwEntity> entity;
    RID handle;
};

BoundScene bind_scene(const Ref<NetwMultiplayer> &p_core, const char *p_stem) {
    BoundScene made;
    made.entity.instantiate();
    if (p_stem != nullptr) {
        made.root = memnew(Node);
        made.root->set_name(p_stem);
        made.entity->attach_to(made.root);
    }
    made.handle = p_core->get_liveness_core()->entity_create();
    made.entity->get_record()->adopt_handle(made.handle);
    if (made.root != nullptr) {
        REQUIRE(p_core->entity_of(made.root) == made.handle);
    }
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SB1 reading a scene's boundary answers the "
    "layer the scene's own id names, so the composition a caller would have "
    "spelled twice is taken here once"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const BoundScene arena = bind_scene(core, "Arena");
    REQUIRE(core->scene_layer_id(arena.handle) == StringName("scene:Arena"));
    const Ref<NetwInterestLayer> opened
        = core->interest_layer(StringName("scene:Arena"));

    const Ref<RefCounted> found = core->scene_layer_view(arena.handle);

    CHECK(found == opened);
    CHECK(opened->get_layer_id() == core->scene_layer_id(arena.handle));

    memdelete(arena.root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SB2 a scene bound to no node names no "
    "boundary and reads no layer, so an empty layer id never resolves as if "
    "it were a layer nobody had opened"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const BoundScene hollow = bind_scene(core, nullptr);

    REQUIRE(core->scene_layer_id(hollow.handle) == StringName());

    CHECK(core->scene_layer_view(hollow.handle).is_null());
    CHECK(core->scene_layer_view(RID()).is_null());
    NETW_CHECK_EQ(core->interest_layers().size(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SB3 reading a boundary never mints one, so a "
    "scene nobody was admitted to reads as no boundary rather than as an "
    "empty one"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const BoundScene arena = bind_scene(core, "Arena");
    netw::interest::Engine &engine = core->interest_plane();

    CHECK(core->scene_layer_view(arena.handle).is_null());

    CHECK_FALSE(engine.has_layer(StringName("scene:Arena")));
    CHECK_FALSE(
        core->interest_layer_find(StringName("scene:Arena")).is_valid()
    );

    memdelete(arena.root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SB5 the peers a scene admits are read off the "
    "boundary the admission wrote, so a release is visible in the same answer "
    "and no roster is kept beside it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const BoundScene arena = bind_scene(core, "Arena");

    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 0);

    REQUIRE(core->scene_admit_peer(arena.handle, 7));
    REQUIRE(core->scene_admit_peer(arena.handle, 9));

    PackedInt32Array seated = core->scene_get_peers(arena.handle);
    NETW_CHECK_EQ(seated.size(), 2);
    CHECK(seated.has(7));
    CHECK(seated.has(9));

    REQUIRE(core->scene_release_peer(arena.handle, 7));

    seated = core->scene_get_peers(arena.handle);
    NETW_CHECK_EQ(seated.size(), 1);
    CHECK_FALSE(seated.has(7));
    CHECK(seated.has(9));

    memdelete(arena.root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SB6 a scene bound to no node admits nobody "
    "rather than everybody, and two live scenes answer their own viewers "
    "rather than the session's"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const BoundScene hollow = bind_scene(core, nullptr);
    const BoundScene arena = bind_scene(core, "Arena");
    const BoundScene annex = bind_scene(core, "Annex");

    REQUIRE(core->scene_admit_peer(arena.handle, 7));

    NETW_CHECK_EQ(core->scene_get_peers(hollow.handle).size(), 0);
    NETW_CHECK_EQ(core->scene_get_peers(RID()).size(), 0);
    NETW_CHECK_EQ(core->scene_get_peers(annex.handle).size(), 0);
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 1);

    memdelete(annex.root);
    memdelete(arena.root);
}

} // namespace TestNetwSceneBoundaryReadLaws
