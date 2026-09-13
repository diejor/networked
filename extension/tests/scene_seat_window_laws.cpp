#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneSeatWindowLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Bound {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Ref<netw::NetwParticipant> a_seat_announcing_row() {
    Ref<netw::NetwParticipant> row;
    row.instantiate();
    return row;
}

Bound bind_entity(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    bool p_declares_scene
) {
    Bound made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record = made.wrapper->get_record();
    made.record->adopt_handle(made.handle);
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
    return made;
}

Bound mount_scene(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const char *p_stem
) {
    Bound made = bind_entity(p_core, p_parent, true);
    Node *level = memnew(Node);
    level->set_name(p_stem);
    made.owner->add_child(level);
    p_core->get_scene_core()
        ->scene_enter(made.handle, StringName(p_stem), false);
    return made;
}

void open_participant(const Ref<NetwMultiplayer> &p_core, int64_t p_peer) {
    p_core->participant_adopt(p_peer, a_seat_announcing_row());
    REQUIRE(p_core->participant_has(p_peer));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS1 a seat is cleared at the settle rather "
    "than at the release edge that asked for it, so a release followed in "
    "the same pump by a re-seat leaves the peer where the re-seat put it "
    "rather than nowhere"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound annex = mount_scene(core, root, "Annex");
    const int64_t peer = 7;
    open_participant(core, peer);
    REQUIRE(core->participant_seat_move(peer, arena.handle));

    core->scene_seat_clear_deferred(peer, arena.handle);

    CHECK(core->settle_has_key(
        NetwMultiplayer::scene_seat_clear_key(peer, arena.handle)
    ));
    CHECK(core->participant_seat(peer) == arena.handle);

    REQUIRE(core->participant_seat_move(peer, annex.handle));
    core->settle_drain();

    CHECK(core->participant_seat(peer) == annex.handle);
    CHECK_FALSE(core->settle_has_key(
        NetwMultiplayer::scene_seat_clear_key(peer, arena.handle)
    ));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS2 the settle is what clears the seat, and "
    "it needs no idle frame: a peer released and left alone through one "
    "drain holds no seat afterwards and its row is told once"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const int64_t peer = 7;
    open_participant(core, peer);
    const CallLog heard;
    core->participant_of(peer)->connect(
        "scene_changed",
        heard.callable("seat")
    );
    REQUIRE(core->participant_seat_move(peer, arena.handle));
    NETW_CHECK_EQ(heard.count("seat"), 1);

    core->scene_seat_clear_deferred(peer, arena.handle);

    NETW_CHECK_EQ(heard.count("seat"), 1);

    core->settle_drain();

    CHECK_FALSE(core->participant_seat(peer).is_valid());
    NETW_CHECK_EQ(heard.count("seat"), 2);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS3 the clear window is keyed per peer and "
    "per scene, so one peer's repeated release edges against one scene "
    "settle once and two peers leaving one scene in a pump are two clears"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const int64_t first = 7;
    const int64_t second = 8;
    open_participant(core, first);
    open_participant(core, second);
    REQUIRE(core->participant_seat_move(first, arena.handle));
    REQUIRE(core->participant_seat_move(second, arena.handle));

    CHECK(
        NetwMultiplayer::scene_seat_clear_key(first, arena.handle)
        != NetwMultiplayer::scene_seat_clear_key(second, arena.handle)
    );

    core->scene_seat_clear_deferred(first, arena.handle);
    core->scene_seat_clear_deferred(first, arena.handle);

    NETW_CHECK_EQ(core->settle_pending(), 1);

    core->scene_seat_clear_deferred(second, arena.handle);

    NETW_CHECK_EQ(core->settle_pending(), 2);

    core->settle_drain();

    CHECK_FALSE(core->participant_seat(first).is_valid());
    CHECK_FALSE(core->participant_seat(second).is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS4 the two windows a departing peer opens "
    "are keyed apart, so a seat release and a membership clear against one "
    "peer and one scene are two settles rather than either swallowing the "
    "other"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const int64_t peer = 7;

    CHECK(
        NetwMultiplayer::scene_seat_clear_key(peer, arena.handle)
        != NetwMultiplayer::scene_seat_release_key(peer, arena.handle)
    );
    CHECK(
        NetwMultiplayer::scene_seat_release_key(peer, arena.handle)
        != NetwMultiplayer::scene_seat_release_key(peer + 1, arena.handle)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS5 a player still standing under the scene "
    "it was seated into keeps that seat, and one that has left the scene "
    "loses it even though it is still alive, which is the difference a "
    "reparent and a free cannot be told apart by at the exit edge alone"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);
    const int64_t peer = 7;

    REQUIRE(core->is_server());
    REQUIRE(core->scene_admit_peer(arena.handle, peer));
    REQUIRE(core->scene_of(pawn.handle) == arena.handle);
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 1);

    CHECK_FALSE(
        core->scene_release_departed(arena.handle, pawn.handle, true, peer)
    );
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 1);

    arena.owner->remove_child(pawn.owner);

    REQUIRE_FALSE(core->scene_of(pawn.handle).is_valid());
    CHECK(core->scene_release_departed(arena.handle, pawn.handle, true, peer));
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 0);

    arena.owner->add_child(pawn.owner);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS6 a mover the caller can no longer answer "
    "for is gone, so its seat is released without the scene being walked, "
    "which is what lets a freed player release the admission its own record "
    "can no longer resolve"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);
    const int64_t peer = 7;

    REQUIRE(core->scene_admit_peer(arena.handle, peer));
    REQUIRE(core->scene_of(pawn.handle) == arena.handle);

    CHECK(core->scene_release_departed(arena.handle, pawn.handle, false, peer));
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 0);

    CHECK_FALSE(
        core->scene_release_departed(arena.handle, pawn.handle, false, peer)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS7 admission is the server's book, so a "
    "client reaching this window releases nobody and the boundary it holds "
    "is left as the server wrote it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Ref<netw::LocalMultiplayerPeer> link;
    link.instantiate();
    link->create_client(42);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(link);
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);
    const int64_t peer = 7;

    REQUIRE_FALSE(core->is_server());
    REQUIRE(core->scene_admit_peer(arena.handle, peer));

    CHECK_FALSE(
        core->scene_release_departed(arena.handle, pawn.handle, false, peer)
    );
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 1);

    memdelete(root);
}

} // namespace TestNetwSceneSeatWindowLaws
