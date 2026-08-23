#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSceneCaptureReadingLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayerCore;
using netw::NetwSceneCore;
using netw::SessionCore;

const char *const SAVED = "res://arena.tscn";

Ref<NetwMultiplayerCore> hosting_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    return core;
}

Ref<NetwMultiplayerCore> client_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_client(42);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

RID live_scene(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_owner,
    bool p_owns_its_world
) {
    const RID handle = p_core->get_liveness_core()->entity_create();
    Ref<NetwEntity> wrapper;
    wrapper.instantiate();
    wrapper->set_scene_isolation(
        p_owns_its_world ? NetwSceneCore::ISOLATION_OWN_WORLD
                         : NetwSceneCore::ISOLATION_NONE
    );
    Ref<netw::NetwEntityRecord> record;
    record.instantiate();
    record->adopt_handle(handle);
    record->set_declares_scene(true);
    REQUIRE(p_core->liveness_bind(
        handle,
        p_core->get_liveness_core()->reserve_route(),
        wrapper,
        record,
        p_owner
    ));
    p_core->get_scene_core()->scene_enter(
        handle,
        StringName("Arena"),
        p_owns_its_world
    );
    return handle;
}

void seat_local(const Ref<NetwMultiplayerCore> &p_core, const RID &p_scene) {
    Ref<RefCounted> row;
    row.instantiate();
    p_core->participant_adopt(p_core->get_unique_id(), row);
    REQUIRE(p_core->participant_admit(p_core->get_unique_id()));
    REQUIRE(p_core->participant_take_seat(p_core->get_unique_id(), p_scene));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SC1 an entry carrying no resource path is "
    "refused whatever else the session reads, because a scene nobody can name "
    "on the wire has no request to become and no file to respawn from"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();
    Node *root = memnew(Node);
    core->get_scene_core()->set_request_reach(NetwSceneCore::REACH_SESSION);
    live_scene(core, root, false);

    NETW_CHECK_EQ(
        core->scene_capture_verdict(String()),
        int(NetwSceneCore::CAPTURE_REFUSED)
    );
    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_CHANGE_SESSION)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SC2 a peer that is not authority turns the "
    "entry into a request and reads nothing else, so a client never decides "
    "for itself what a native scene change means"
) {
    const Ref<NetwMultiplayerCore> core = client_core();
    Node *root = memnew(Node);
    core->get_scene_core()->set_request_reach(NetwSceneCore::REACH_SESSION);
    const RID arena = live_scene(core, root, true);
    core->session_plane().set_role(SessionCore::ROLE_CLIENT);
    seat_local(core, arena);

    REQUIRE_FALSE(core->is_server());
    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_REQUEST)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SC3 authority replaces the whole session's "
    "presentation only when the reach says so AND a scene is already live, so "
    "the first scene of a session is activated rather than transitioned into"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();
    Node *root = memnew(Node);
    core->session_plane().set_role(SessionCore::ROLE_DEDICATED_SERVER);
    core->get_scene_core()->set_request_reach(NetwSceneCore::REACH_SESSION);

    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );

    live_scene(core, root, false);

    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_CHANGE_SESSION)
    );

    core->get_scene_core()->set_request_reach(NetwSceneCore::REACH_PARTICIPANT);

    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SC4 a listen host seated in a world of its own "
    "reads the bare call as move-me, which is the meaning a client's bare call "
    "already carries, so the host relocates rather than spawning a world it "
    "does not enter"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();
    Node *root = memnew(Node);
    core->session_plane().set_role(SessionCore::ROLE_LISTEN_SERVER);
    const RID arena = live_scene(core, root, true);

    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );

    seat_local(core, arena);

    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_MOVE_ME)
    );

    Node *annex_root = memnew(Node);
    const RID annex = live_scene(core, annex_root, false);
    REQUIRE(core->participant_take_seat(core->get_unique_id(), annex));

    NETW_CHECK_EQ(
        core->scene_capture_verdict(SAVED),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );

    memdelete(annex_root);
    memdelete(root);
}

} // namespace TestNetwSceneCaptureReadingLaws
