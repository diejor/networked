#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestParticipantDisplayLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Placed {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
    Node *world = nullptr;
};

Placed place(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    Node *p_world,
    const StringName &p_stem
) {
    Placed made;
    made.owner = memnew(Node);
    made.owner->set_name(p_stem.is_empty() ? StringName("Pawn") : p_stem);
    made.world = p_world;
    if (made.world != nullptr) {
        made.world->set_meta(NetwMultiplayer::scene_container_meta(), true);
        p_parent->add_child(made.world);
        made.world->add_child(made.owner);
    } else {
        p_parent->add_child(made.owner);
    }
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.wrapper->set_rid_handle(made.handle);
    made.record = made.wrapper->get_record();
    made.record->set_declares_scene(!p_stem.is_empty());
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
    if (!p_stem.is_empty()) {
        p_core->get_scene_core()->scene_enter(made.handle, p_stem, true);
    }
    return made;
}

Placed isolate(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    return place(p_core, p_parent, memnew(SubViewport), p_stem);
}

Placed share(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    return place(p_core, p_parent, nullptr, p_stem);
}

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return core;
}

Placed stand(const Ref<NetwMultiplayer> &p_core, const Placed &p_scene) {
    const Placed pawn = share(p_core, p_scene.owner, StringName());
    pawn.wrapper->set_peer_id(p_core->get_unique_id());
    p_core->scene_player_display_invalidate();
    return pawn;
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD1 only a listen host answers a player "
    "viewport, so one presented isolated world reads as a display on a host "
    "and as nothing on a client, a dedicated server and a session that has "
    "resolved no role at all"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));
    stand(core, arena);

    NETW_CHECK_EQ(int(core->scene_player_viewport() == arena.world), 1);

    core->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    NETW_CHECK_EQ(int(core->scene_player_viewport() != nullptr), 0);

    core->session_set_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    NETW_CHECK_EQ(int(core->scene_player_viewport() != nullptr), 0);

    core->session_set_role(NetwMultiplayer::ROLE_NONE);
    NETW_CHECK_EQ(int(core->scene_player_viewport() != nullptr), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD2 the display is the world the host's own "
    "body stands in, so a host holding two live isolated worlds draws the one "
    "it is in and never the one the live book answers first"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));
    const Placed annex = isolate(core, root, StringName("Annex"));

    const Placed pawn = stand(core, annex);
    NETW_CHECK_EQ(int(core->scene_player_viewport() == annex.world), 1);

    annex.owner->remove_child(pawn.owner);
    arena.owner->add_child(pawn.owner);
    core->scene_player_display_invalidate();

    NETW_CHECK_EQ(int(core->scene_player_viewport() == arena.world), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD3 a host standing in no world draws "
    "nothing, however many live isolated worlds stand beside it, because a "
    "display that picked one of them would be picking for the game"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    isolate(core, root, StringName("Arena"));
    isolate(core, root, StringName("Annex"));

    CHECK_FALSE(core->scene_presented().is_valid());
    NETW_CHECK_EQ(int(core->scene_player_viewport() != nullptr), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD4 a presented scene sharing the session's "
    "world mounts no viewport to draw, and the isolated world standing beside "
    "it is not offered in its place"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed lobby = share(core, root, StringName("Lobby"));
    isolate(core, root, StringName("Arena"));

    stand(core, lobby);

    CHECK(core->scene_presented() == lobby.handle);
    NETW_CHECK_EQ(int(core->scene_player_viewport() != nullptr), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD6 a body belonging to another peer draws "
    "that peer's world on nobody's screen, so a host watching a client "
    "teleport keeps the display it holds"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));
    const Placed annex = isolate(core, root, StringName("Annex"));
    stand(core, arena);

    const Placed guest = share(core, annex.owner, StringName());
    guest.wrapper->set_peer_id(int64_t(core->get_unique_id()) + 1);
    core->scene_player_display_invalidate();

    NETW_CHECK_EQ(int(core->scene_player_viewport() == arena.world), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD5 the display is announced on the turn-over "
    "alone, so a settle that resolves what the last one did stays silent and "
    "a host is not told to re-adopt a camera it already holds"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));
    const CallLog announced;
    core->connect(
        StringName("participant_viewport_changed"),
        announced.callable("changed")
    );
    const Placed pawn = stand(core, arena);

    core->scene_player_display_settle();
    NETW_CHECK_EQ(int(announced.count(StringName("changed"))), 1);

    core->scene_player_display_settle();
    NETW_CHECK_EQ(int(announced.count(StringName("changed"))), 1);

    arena.owner->remove_child(pawn.owner);
    root->add_child(pawn.owner);
    core->scene_player_display_invalidate();
    core->scene_player_display_settle();
    NETW_CHECK_EQ(int(announced.count(StringName("changed"))), 2);

    memdelete(root);
}

} // namespace TestParticipantDisplayLaws
