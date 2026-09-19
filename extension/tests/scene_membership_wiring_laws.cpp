#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/scene_core.hpp"
#include "support/joined_peer.h"
#include "support/netw_call_log.h"

namespace TestSceneMembershipWiringLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Placed {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Placed place(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    bool p_declares_scene,
    const StringName &p_stem = StringName("Arena")
) {
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
        level->set_name(p_stem);
        made.owner->add_child(level);
        p_core->get_scene_core()->scene_enter(made.handle, p_stem, false);
    }
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW2 a player leaving its scene gives the "
    "subscription back at the settle and not in the frame the body left, so "
    "delivery outlives the exit that has not been resolved yet"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);
    pawn.record->set_peer_id(7);
    REQUIRE(netw_test::seated_peer(core.ptr(), 7).is_valid());
    core->membership_place_body(pawn.wrapper);
    CHECK(core->scene_subscribes(arena.handle, 7));

    core->entity_capture_exit(pawn.wrapper.ptr());
    arena.owner->remove_child(pawn.owner);

    CHECK(core->scene_subscribes(arena.handle, 7));

    core->session_flush_deferred();

    CHECK_FALSE(core->scene_subscribes(arena.handle, 7));

    memdelete(pawn.owner);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW5 a player leaving releases the scene its "
    "body is in rather than the scene it was in when the hook was installed, "
    "so a body that moved between scenes releases the one it is actually "
    "leaving"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed first = place(core, root, true, StringName("First"));
    const Placed second = place(core, root, true, StringName("Second"));
    const Placed pawn = place(core, first.owner, false);

    pawn.record->set_peer_id(7);
    REQUIRE(netw_test::seated_peer(core.ptr(), 7).is_valid());
    core->membership_place_body(pawn.wrapper);
    NETW_CHECK_EQ(int(core->scene_watch(first.handle, 7)), int(OK));

    first.owner->remove_child(pawn.owner);
    second.owner->add_child(pawn.owner);
    core->membership_place_body(pawn.wrapper);

    core->entity_capture_exit(pawn.wrapper.ptr());
    second.owner->remove_child(pawn.owner);
    core->session_flush_deferred();

    CHECK_FALSE(core->scene_subscribes(second.handle, 7));
    CHECK(core->scene_subscribes(first.handle, 7));

    memdelete(pawn.owner);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW3 an arrival announces the move and writes "
    "no subscription of its own, because the body's residency is what "
    "subscribes and a second writer would disagree with it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const CallLog moved;
    core->connect(StringName("scene_entity_moved"), moved.callable("moved"));
    Node *root = memnew(Node);
    const Placed source = place(core, root, true, StringName("Source"));
    const Placed destination = place(core, root, true, StringName("Dest"));
    const Placed pawn = place(core, source.owner, false);
    pawn.wrapper->set_peer_id(7);
    REQUIRE(netw_test::seated_peer(core.ptr(), 7).is_valid());
    core->membership_place_body(pawn.wrapper);
    REQUIRE(core->scene_subscribes(source.handle, 7));

    core->scene_arrive(
        pawn.wrapper,
        source.owner,
        destination.owner,
        Ref<netw::NetwPromise>()
    );

    NETW_CHECK_EQ(moved.count(StringName("moved")), 1);
    CHECK(core->scene_subscribes(source.handle, 7));
    CHECK_FALSE(core->scene_subscribes(destination.handle, 7));

    source.owner->remove_child(pawn.owner);
    destination.owner->add_child(pawn.owner);
    core->membership_place_body(pawn.wrapper);

    CHECK(core->scene_subscribes(destination.handle, 7));
    CHECK_FALSE(core->scene_subscribes(source.handle, 7));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW4 unwatching tells the peer before the "
    "subscription goes away, so a peer unwatched from the scene it was "
    "watching ends subscribed to nothing rather than to a scene the server "
    "no longer delivers"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const int64_t here = core->get_unique_id();

    Ref<netw::NetwPlayer> row;
    row.instantiate();
    core->player_adopt(here, row);
    REQUIRE(core->player_admit(here));
    NETW_CHECK_EQ(int(core->scene_watch(arena.handle, here)), int(OK));
    REQUIRE(core->scene_subscribes(arena.handle, here));

    CHECK(core->scene_unwatch(arena.handle, here));

    CHECK_FALSE(core->scene_subscribes(arena.handle, here));

    memdelete(root);
}

} // namespace TestSceneMembershipWiringLaws
