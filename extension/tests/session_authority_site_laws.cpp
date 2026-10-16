#include "support/fault_log.h"
#include "support/mesh_stand.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSessionAuthoritySites {

#if defined(NETW_TIER_HOSTED)

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw_test::MeshStand;

struct Placed {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Placed place(NetwMultiplayer *p_core, Node *p_parent, bool p_declares_scene) {
    Placed made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.wrapper->set_rid_handle(made.handle);
    made.record = made.wrapper->get_record();
    made.record->set_declares_scene(p_declares_scene);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.owner
    ));
    made.owner->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    if (p_declares_scene) {
        Node *level = memnew(Node);
        level->set_name(StringName("Arena"));
        made.owner->add_child(level);
        p_core->get_scene_core()
            ->scene_enter(made.handle, StringName("Arena"), false);
    }
    return made;
}

struct SiteReading {
    bool authority = false;
    bool sends_to_member = false;
    bool subscribes_entity = false;
    int viewer_frames = 0;
    Error kick = OK;
};

SiteReading read_sites(MeshStand &p_mesh, int p_at, int p_member) {
    SiteReading reading;
    NetwMultiplayer *core = p_mesh.session_of(p_at);
    reading.authority = core->is_host();
    reading.sends_to_member = core->interest_can_send_to(p_member);

    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);
    pawn.record->set_peer_id(9);
    core->membership_place_entity(pawn.wrapper);
    reading.subscribes_entity = core->scene_subscribes(arena.handle, 9);

    p_mesh.drain_all();
    core->scene_publish_viewers(arena.handle);
    reading.viewer_frames = p_mesh.capture_at(p_member).size();

    const Ref<NetwPlayer> nine = core->player_of(9);
    REQUIRE(nine.is_valid());
    reading.kick = core->player_kick(nine, String("site law"));

    memdelete(root);
    return reading;
}

TEST_CASE(
    "[Networked][Session] SA1 the server publishes scene viewers, places "
    "membership entities, may send interest and kicks, while a member does "
    "none of them"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] Condition \"!is_host()\" is true. "
        "Returning: ERR_UNAUTHORIZED. NetwSession.kick runs only on"
        " the server.",
        1
    );

    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(7);
    mesh.seat_member(9);
    mesh.wire(1, 7);
    mesh.wire(1, 9);
    mesh.pump(4);
    REQUIRE(mesh.join(1, StringName("one")).is_valid());
    REQUIRE(mesh.join(7, StringName("seven")).is_valid());
    REQUIRE(mesh.join(9, StringName("nine")).is_valid());

    const SiteReading seven = read_sites(mesh, 7, 1);
    CHECK_FALSE(seven.authority);
    CHECK_FALSE(seven.sends_to_member);
    CHECK_FALSE(seven.subscribes_entity);
    NETW_CHECK_EQ(seven.viewer_frames, 0);
    NETW_CHECK_EQ(int(seven.kick), int(ERR_UNAUTHORIZED));

    const SiteReading one = read_sites(mesh, 1, 7);
    CHECK(one.authority);
    CHECK(one.sends_to_member);
    CHECK(one.subscribes_entity);
    CHECK(one.viewer_frames > 0);
    NETW_CHECK_EQ(int(one.kick), int(OK));
}

#endif

} // namespace TestNetwSessionAuthoritySites
