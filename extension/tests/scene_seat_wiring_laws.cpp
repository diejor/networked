#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestSceneSeatWiringLaws {

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
    "[Networked][Scene][Hosted] SW1 the scene a session presents is the seat "
    "its own participant holds, and a dedicated server holds no participant "
    "to present through, so the same live book answers a scene on a host and "
    "nothing on a server that only runs it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);

    const Ref<netw::NetwParticipant> local = core->participant_ensure(1);
    REQUIRE(local.is_valid());
    core->scene_bind_local_participant(local);
    CHECK(core->participant_take_seat(1, arena.handle));

    NETW_CHECK_EQ(int(core->participant_seat(1) == arena.handle), 1);

    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    core->scene_refresh_current();
    NETW_CHECK_EQ(
        int(core->get_scene_core()->get_current_scene() == arena.handle),
        1
    );

    core->session_set_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    core->scene_refresh_current();
    NETW_CHECK_EQ(
        int(core->get_scene_core()->get_current_scene().is_valid()),
        0
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW2 a player leaving its scene gives the seat "
    "back at the settle and not in the frame the body left, so the admission "
    "outlives the exit that has not been resolved yet"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);
    pawn.record->set_peer_id(7);
    NETW_CHECK_EQ(int(core->scene_admit(arena.handle, 7)), int(OK));
    CHECK(core->scene_admits(arena.handle, 7));

    core->entity_capture_exit(pawn.wrapper.ptr());
    arena.owner->remove_child(pawn.owner);

    CHECK(core->scene_admits(arena.handle, 7));

    core->session_flush_deferred();

    CHECK_FALSE(core->scene_admits(arena.handle, 7));

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
    NETW_CHECK_EQ(int(core->scene_admit(first.handle, 7)), int(OK));
    NETW_CHECK_EQ(int(core->scene_admit(second.handle, 7)), int(OK));

    first.owner->remove_child(pawn.owner);
    second.owner->add_child(pawn.owner);

    core->entity_capture_exit(pawn.wrapper.ptr());
    second.owner->remove_child(pawn.owner);
    core->session_flush_deferred();

    CHECK_FALSE(core->scene_admits(second.handle, 7));
    CHECK(core->scene_admits(first.handle, 7));

    memdelete(pawn.owner);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW3 an entity landing in a scene moves its "
    "peer's seat to the scene it landed in, so the participant record and the "
    "body agree without anyone asking the tree where the body ended up"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed source = place(core, root, true, StringName("Source"));
    const Placed destination = place(core, root, true, StringName("Dest"));
    const Placed pawn = place(core, source.owner, false);
    pawn.wrapper->set_peer_id(7);
    REQUIRE(core->participant_ensure(7).is_valid());
    CHECK(core->participant_take_seat(7, source.handle));
    NETW_CHECK_EQ(int(core->participant_seat(7) == source.handle), 1);
    NETW_CHECK_EQ(
        int(core->scene_of(core->entity_of(destination.owner))
            == destination.handle),
        1
    );

    core->scene_arrive(
        pawn.wrapper,
        source.owner,
        destination.owner,
        Ref<netw::NetwPromise>()
    );

    NETW_CHECK_EQ(int(core->participant_seat(7) == destination.handle), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW4 releasing a seat tells the peer holding "
    "it before the membership goes away, so a peer released from the scene it "
    "is standing in ends holding no seat rather than one the server no longer "
    "admits it to"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const int64_t here = core->get_unique_id();

    Ref<netw::NetwParticipant> row;
    row.instantiate();
    core->participant_adopt(here, row);
    REQUIRE(core->participant_admit(here));
    NETW_CHECK_EQ(int(core->scene_admit(arena.handle, here)), int(OK));
    core->participant_take_seat(here, arena.handle);
    NETW_CHECK_EQ(int(core->participant_seat(here) == arena.handle), 1);

    CHECK(core->scene_release(arena.handle, here));

    CHECK_FALSE(core->scene_admits(arena.handle, here));
    NETW_CHECK_EQ(int(core->participant_seat(here).is_valid()), 0);

    memdelete(root);
}

} // namespace TestSceneSeatWiringLaws
