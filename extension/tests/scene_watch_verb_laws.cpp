#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/wire/registry.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneWatchVerbLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

Ref<NetwMultiplayer> peered_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

Ref<netw::NetwPlayer> adopt_player(
    const Ref<NetwMultiplayer> &p_core,
    int64_t p_peer
) {
    Ref<netw::NetwPlayer> row;
    row.instantiate();
    p_core->player_adopt(p_peer, row);
    p_core->player_publish_joined(p_peer);
    REQUIRE(p_core->player_has(p_peer));
    return row;
}

struct Declared {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Declared declare_scene(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const char *p_stem
) {
    Declared made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    Node *level = memnew(Node);
    level->set_name(p_stem);
    made.owner->add_child(level);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record = made.wrapper->get_record();
    made.record->adopt_handle(made.handle);
    made.record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.owner
    ));
    made.owner->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    REQUIRE(!p_core->scene_layer_id(made.handle).is_empty());
    return made;
}

int64_t declared_channel_id(const char *p_name) {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    const netw::wire::ChannelDecl *decl
        = registry.find_channel_by_name(StringName(p_name));
    REQUIRE(decl != nullptr);
    return int64_t(decl->id);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP1 a watch names one scene and adds itself "
    "to what the peer already reaches, so a player watching a second "
    "scene is in both and the first is not traded away for it"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const Declared annex = declare_scene(core, root, "Annex");
    const int64_t peer = 7;
    adopt_player(core, peer);
    netw::interest::Engine &engine = core->interest_plane();
    const StringName arena_layer = core->scene_layer_id(arena.handle);
    const StringName annex_layer = core->scene_layer_id(annex.handle);

    CHECK_FALSE(engine.layer_has_viewer(arena_layer, peer));

    NETW_CHECK_EQ(int(core->scene_watch(arena.handle, peer)), int(OK));
    CHECK(engine.layer_has_viewer(arena_layer, peer));
    CHECK_FALSE(engine.layer_has_viewer(annex_layer, peer));

    NETW_CHECK_EQ(int(core->scene_watch(annex.handle, peer)), int(OK));
    CHECK(engine.layer_has_viewer(arena_layer, peer));
    CHECK(engine.layer_has_viewer(annex_layer, peer));

    CHECK(core->scene_unwatch(annex.handle, peer));
    CHECK(engine.layer_has_viewer(arena_layer, peer));
    CHECK_FALSE(engine.layer_has_viewer(annex_layer, peer));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP2 a watch the session cannot seat spends "
    "nothing: peer zero and a handle naming no live container are both "
    "refused, and watching what is already watched changes no boundary"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const int64_t peer = 7;
    adopt_player(core, peer);
    netw::interest::Engine &engine = core->interest_plane();
    const StringName arena_layer = core->scene_layer_id(arena.handle);

    NETW_CHECK_EQ(
        int(core->scene_watch(arena.handle, 0)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(
        int(core->scene_watch(RID(), peer)),
        int(ERR_DOES_NOT_EXIST)
    );
    CHECK_FALSE(engine.layer_has_viewer(arena_layer, peer));

    NETW_CHECK_EQ(int(core->scene_watch(arena.handle, peer)), int(OK));
    NETW_CHECK_EQ(int(core->scene_watch(arena.handle, peer)), int(OK));

    CHECK(engine.layer_has_viewer(arena_layer, peer));
    NETW_CHECK_EQ(int(core->scene_get_viewers(arena.handle).size()), 1);

    CHECK_FALSE(core->scene_unwatch(RID(), peer));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP3 the release notice is spent on a peer "
    "that actually loses the scene, so watching a second scene tells nobody "
    "anything and a peer still subscribed hears nothing either"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    core->get_channel_book()->register_protocol(
        declared_channel_id("SESSION_SCENE_RELEASED"),
        seen.callable("released")
    );
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const Declared annex = declare_scene(core, root, "Annex");
    const int64_t local = int64_t(core->get_unique_id());
    adopt_player(core, local);

    NETW_CHECK_EQ(int(core->scene_watch(arena.handle, local)), int(OK));
    NETW_CHECK_EQ(int(core->scene_watch(annex.handle, local)), int(OK));
    NETW_CHECK_EQ(seen.count("released"), 0);

    CHECK(core->scene_unwatch(arena.handle, local));
    NETW_CHECK_EQ(seen.count("released"), 1);

    CHECK(core->scene_unwatch(arena.handle, local));
    NETW_CHECK_EQ(seen.count("released"), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP4 unwatching takes the boundary and the "
    "viewer roster together, so the two published readers can never disagree "
    "about whether a player reaches the scene"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const int64_t peer = 7;
    adopt_player(core, peer);
    const StringName layer = core->scene_layer_id(arena.handle);

    REQUIRE(core->scene_watch(arena.handle, peer) == OK);
    REQUIRE(core->scene_subscribes(arena.handle, peer));
    NETW_CHECK_EQ(int(core->scene_get_viewers(arena.handle).size()), 1);

    CHECK(core->scene_unwatch(arena.handle, peer));

    NETW_CHECK_EQ(int(core->scene_subscribes(arena.handle, peer)), 0);
    NETW_CHECK_EQ(
        int(core->interest_plane().layer_has_viewer(layer, peer)),
        0
    );
    NETW_CHECK_EQ(int(core->scene_get_viewers(arena.handle).size()), 0);

    memdelete(root);
}

} // namespace TestNetwSceneWatchVerbLaws
