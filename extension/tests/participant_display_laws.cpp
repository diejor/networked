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

TEST_CASE(
    "[Networked][Scene][Hosted] PD1 only a listen host answers a participant "
    "viewport, so the same live isolated world reads as a display on a host "
    "and as nothing on a client, a dedicated server and a session that has "
    "resolved no role at all"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));

    NETW_CHECK_EQ(int(core->scene_participant_viewport() == arena.world), 1);

    core->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    NETW_CHECK_EQ(int(core->scene_participant_viewport() != nullptr), 0);

    core->session_set_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    NETW_CHECK_EQ(int(core->scene_participant_viewport() != nullptr), 0);

    core->session_set_role(NetwMultiplayer::ROLE_NONE);
    NETW_CHECK_EQ(int(core->scene_participant_viewport() != nullptr), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD2 the scene the session presents is the "
    "display, so a host holding the second of two live isolated worlds as "
    "its current scene draws that one rather than the one the live book "
    "answers first"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));
    const Placed annex = isolate(core, root, StringName("Annex"));

    NETW_CHECK_EQ(int(core->scene_participant_viewport() == arena.world), 1);

    core->get_scene_core()->set_current_scene(annex.handle);

    NETW_CHECK_EQ(int(core->scene_participant_viewport() == annex.world), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD3 a scene sharing the session's world "
    "mounts no viewport to draw, so a host running one beside an isolated "
    "world answers the isolated one rather than the first scene it walks"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    share(core, root, StringName("Lobby"));
    const Placed arena = isolate(core, root, StringName("Arena"));

    NETW_CHECK_EQ(int(core->scene_participant_viewport() == arena.world), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD4 a scene whose level is disabled is not a "
    "display, so a host between worlds passes over the suspended one and "
    "answers the world still running"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed parked = isolate(core, root, StringName("Parked"));
    const Placed arena = isolate(core, root, StringName("Arena"));
    parked.owner->set_process_mode(Node::PROCESS_MODE_DISABLED);

    NETW_CHECK_EQ(int(core->scene_participant_viewport() == arena.world), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] PD6 a host standing in a world draws the "
    "world it stands in, so the scene holding the local player outranks the "
    "one the session presents and a spectator's fallback never steals the "
    "display from a seated host"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Placed arena = isolate(core, root, StringName("Arena"));
    const Placed annex = isolate(core, root, StringName("Annex"));
    core->get_scene_core()->set_current_scene(annex.handle);
    const Placed pawn = share(core, arena.owner, StringName());
    pawn.wrapper->set_peer_id(core->get_unique_id());

    NETW_CHECK_EQ(int(core->get_unique_id() != 0), 1);
    NETW_CHECK_EQ(int(core->scene_participant_player() == pawn.owner), 1);
    NETW_CHECK_EQ(int(core->scene_participant_viewport() == arena.world), 1);

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

    core->scene_participant_display_settle();
    NETW_CHECK_EQ(int(announced.count(StringName("changed"))), 1);

    core->scene_participant_display_settle();
    NETW_CHECK_EQ(int(announced.count(StringName("changed"))), 1);

    core->get_scene_core()->scene_exit(arena.handle);
    core->scene_participant_display_settle();
    NETW_CHECK_EQ(int(announced.count(StringName("changed"))), 2);

    memdelete(root);
}

} // namespace TestParticipantDisplayLaws
