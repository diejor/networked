#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneMembershipWindowLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Bound {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Ref<netw::NetwPlayer> a_player_row() {
    Ref<netw::NetwPlayer> row;
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
    made.wrapper->set_owner(made.owner);
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

void open_player(const Ref<NetwMultiplayer> &p_core, int64_t p_peer) {
    p_core->player_adopt(p_peer, a_player_row());
    REQUIRE(p_core->player_has(p_peer));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS5 a player still standing under the scene "
    "its body resides in keeps that subscription, and one whose body has "
    "left loses it even though the body is still alive, which is the "
    "difference a reparent and a free cannot be told apart by at the exit "
    "edge alone"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);
    const int64_t peer = 7;
    open_player(core, peer);
    pawn.record->set_peer_id(peer);

    REQUIRE(core->is_server());
    core->membership_place_body(pawn.wrapper);
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
    "for is gone, so its reason is dropped without the scene being walked, "
    "which is what lets a freed player release the subscription its own "
    "record can no longer resolve"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);
    const int64_t peer = 7;
    open_player(core, peer);
    pawn.record->set_peer_id(peer);

    core->membership_place_body(pawn.wrapper);
    REQUIRE(core->scene_of(pawn.handle) == arena.handle);

    CHECK(core->scene_release_departed(arena.handle, pawn.handle, false, peer));
    NETW_CHECK_EQ(core->scene_get_peers(arena.handle).size(), 0);

    CHECK_FALSE(
        core->scene_release_departed(arena.handle, pawn.handle, false, peer)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS7 the subscription book is the server's, so "
    "a client reaching this window releases nobody and the boundary it holds "
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

TEST_CASE(
    "[Networked][Scene][Hosted] SS8 a watch and a body are separate reasons "
    "for one scene, so a player watching a lobby while a body of theirs "
    "resides in an arena subscribes to both, and losing either leaves the "
    "other standing"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound lobby = mount_scene(core, root, "Lobby");
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);
    const int64_t peer = 7;
    open_player(core, peer);
    pawn.record->set_peer_id(peer);

    REQUIRE(core->is_server());
    core->membership_place_body(pawn.wrapper);
    REQUIRE(core->scene_subscribes(arena.handle, peer));
    CHECK_FALSE(core->scene_subscribes(lobby.handle, peer));

    REQUIRE(core->scene_admit_peer(lobby.handle, peer));

    CHECK(core->scene_subscribes(lobby.handle, peer));
    CHECK(core->scene_subscribes(arena.handle, peer));

    core->membership_place_body(pawn.wrapper);

    CHECK(core->scene_subscribes(lobby.handle, peer));
    CHECK(core->scene_subscribes(arena.handle, peer));

    core->scene_release_peer(lobby.handle, peer);

    CHECK_FALSE(core->scene_subscribes(lobby.handle, peer));
    CHECK(core->scene_subscribes(arena.handle, peer));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS9 a player answers the bodies it owns "
    "and no others, so two players standing in one scene read as one body "
    "each rather than as everything the scene holds"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound mine = bind_entity(core, arena.owner, false);
    const Bound theirs = bind_entity(core, arena.owner, false);
    open_player(core, 7);
    open_player(core, 9);
    mine.record->set_peer_id(7);
    mine.record->set_player_id(core->player_incarnation(7));
    theirs.record->set_peer_id(9);
    theirs.record->set_player_id(core->player_incarnation(9));

    NETW_CHECK_EQ(int(core->scene_get_bodies(arena.handle).size()), 2);

    const TypedArray<netw::NetwEntity> ours = core->player_bodies(7);
    NETW_CHECK_EQ(int(ours.size()), 1);
    const Ref<netw::NetwEntity> only = ours[0];
    CHECK(only == mine.wrapper);

    NETW_CHECK_EQ(int(core->player_bodies(9).size()), 1);
    CHECK(core->player_bodies(11).is_empty());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS10 a body outliving the membership it was "
    "spawned for stays with that membership, so the next player to connect "
    "onto the same peer id reads an empty hand rather than a dead player's "
    "bodies"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound abandoned = bind_entity(core, arena.owner, false);

    open_player(core, 7);
    const Ref<netw::NetwPlayer> departed = core->player_of(7);
    abandoned.record->set_peer_id(7);
    abandoned.record->set_player_id(departed->player_id());
    NETW_CHECK_EQ(int(departed->get_bodies().size()), 1);

    core->player_release_id(departed);
    open_player(core, 7);
    const Ref<netw::NetwPlayer> arrived = core->player_of(7);
    CHECK(arrived->player_id() != departed->player_id());

    CHECK(arrived->get_bodies().is_empty());
    CHECK(core->player_bodies(7).is_empty());
    NETW_CHECK_EQ(int(core->scene_get_bodies(arena.handle).size()), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SS11 a body drops out of every roster in the "
    "call that despawned it rather than in the frame the node is freed, so a "
    "game that opens the next round immediately still reads an empty hand"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Bound arena = mount_scene(core, root, "Arena");
    const Bound pawn = bind_entity(core, arena.owner, false);

    open_player(core, 7);
    const Ref<netw::NetwPlayer> player = core->player_of(7);
    pawn.record->set_peer_id(7);
    pawn.record->set_player_id(player->player_id());
    NETW_CHECK_EQ(int(player->get_bodies().size()), 1);
    NETW_CHECK_EQ(int(core->scene_get_bodies(arena.handle).size()), 1);

    core->entity_despawn(pawn.handle, Ref<netw::NetwDespawnOpts>());

    CHECK(player->get_bodies().is_empty());
    CHECK(core->player_bodies(7).is_empty());
    NETW_CHECK_EQ(int(core->scene_get_bodies(arena.handle).size()), 0);

    memdelete(root);
}

} // namespace TestNetwSceneMembershipWindowLaws
