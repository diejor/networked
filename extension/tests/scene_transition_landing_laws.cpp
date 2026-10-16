#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/scene.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneTransitionLandingLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Declared {
    Ref<netw::NetwEntity> wrapper;
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
    made.owner->set_name(p_stem);
    p_parent->add_child(made.owner);
    Node *level = memnew(Node);
    level->set_name("Level");
    made.owner->add_child(level);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.wrapper->get_record()->adopt_handle(made.handle);
    made.wrapper->get_record()->set_declares_scene(true);
    REQUIRE(p_core->liveness_bind(
        made.handle,
        p_core->get_liveness_core()->reserve_route(),
        made.wrapper,
        made.wrapper->get_record(),
        made.owner
    ));
    made.wrapper->set_owner(made.owner);
    made.owner->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    p_core->get_scene_core()
        ->scene_enter(made.handle, StringName(p_stem), false);
    return made;
}

Ref<NetwMultiplayer> peered_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

void adopt(const Ref<NetwMultiplayer> &p_core, int64_t p_peer) {
    Ref<netw::NetwPlayer> row;
    row.instantiate();
    p_core->player_adopt(p_peer, row);
    p_core->player_publish_joined(p_peer);
    REQUIRE(p_core->player_admit(p_peer));
}

Array one_source(Node *p_source) {
    Array out;
    out.push_back(p_source);
    return out;
}

Declared place_body(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    int64_t p_peer
) {
    Declared made;
    made.owner = memnew(Node);
    made.owner->set_name("Pawn");
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.wrapper->get_record()->adopt_handle(made.handle);
    REQUIRE(p_core->liveness_bind(
        made.handle,
        p_core->get_liveness_core()->reserve_route(),
        made.wrapper,
        made.wrapper->get_record(),
        made.owner
    ));
    made.wrapper->set_owner(made.owner);
    made.wrapper->get_record()->set_peer_id(p_peer);
    made.wrapper->get_record()->set_player_id(
        p_core->player_incarnation(p_peer)
    );
    made.owner->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    p_core->membership_place_body(made.wrapper);
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] TL1 a session-scoped landing seats "
    "every "
    "accepted player on the destination, including one that was "
    "watching nothing at all, because converging is what the scope means"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Declared source = declare_scene(core, root, "Source");
    const Declared elsewhere = declare_scene(core, root, "Elsewhere");
    const Declared target = declare_scene(core, root, "Target");
    adopt(core, 7);
    adopt(core, 9);
    adopt(core, 11);
    REQUIRE(core->scene_watch(source.handle, 7) == OK);
    REQUIRE(core->scene_watch(elsewhere.handle, 9) == OK);

    core->scene_replace_sources(
        target.owner,
        one_source(source.owner),
        NetwMultiplayer::SCENE_CHANGE_SESSION
    );

    CHECK(core->scene_subscribes(target.handle, 7));
    CHECK(core->scene_subscribes(target.handle, 9));
    CHECK(core->scene_subscribes(target.handle, 11));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] TL2 a scene-scoped landing carries "
    "the "
    "watches the source held and nobody else, so a player watching "
    "another world stays there and one watching nothing is left alone"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Declared source = declare_scene(core, root, "Source");
    const Declared elsewhere = declare_scene(core, root, "Elsewhere");
    const Declared target = declare_scene(core, root, "Target");
    adopt(core, 7);
    adopt(core, 9);
    adopt(core, 11);
    REQUIRE(core->scene_watch(source.handle, 7) == OK);
    REQUIRE(core->scene_watch(elsewhere.handle, 9) == OK);

    core->scene_replace_sources(
        target.owner,
        one_source(source.owner),
        NetwMultiplayer::SCENE_CHANGE_SCENE
    );

    CHECK(core->scene_subscribes(target.handle, 7));
    CHECK_FALSE(core->scene_subscribes(target.handle, 9));
    CHECK_FALSE(core->scene_subscribes(target.handle, 11));
    CHECK(core->scene_subscribes(elsewhere.handle, 9));
    CHECK_FALSE(core->scene_watches(source.handle, 7));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] TL3 a scene-scoped change names the world its "
    "caller stands in, so a caller standing in none is refused and the live "
    "book is never read for a source it did not name"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Declared source = declare_scene(core, root, "Source");
    const Declared target = declare_scene(core, root, "Target");
    Node *outsider = memnew(Node);
    root->add_child(outsider);
    adopt(core, 7);

    NETW_CHECK_EQ(
        int(core->scene_sources_for_scope(
                    NetwMultiplayer::SCENE_CHANGE_SCENE,
                    nullptr
        )
                .size()),
        0
    );
    NETW_CHECK_EQ(
        int(core->scene_sources_for_scope(
                    NetwMultiplayer::SCENE_CHANGE_SCENE,
                    outsider
        )
                .size()),
        0
    );

    const Ref<netw::NetwPromise> refused = core->scene_apply_change(
        core->player_of(7),
        target.owner,
        NetwMultiplayer::SCENE_CHANGE_SCENE,
        nullptr
    );

    REQUIRE(refused.is_valid());
    CHECK(refused->get_is_settled());
    NETW_CHECK_EQ(refused->get_code(), int(ERR_INVALID_PARAMETER));
    CHECK(core->get_scene_core()->is_live(source.handle));

    NETW_CHECK_EQ(
        int(core->scene_sources_for_scope(
                    NetwMultiplayer::SCENE_CHANGE_SCENE,
                    source.owner
        )
                .size()),
        1
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] TL8 a change carries no body into "
    "the "
    "destination, so an entity standing in the source is left where it "
    "stands and the destination is reached by the watch alone"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Declared source = declare_scene(core, root, "Source");
    const Declared target = declare_scene(core, root, "Target");
    adopt(core, 7);
    const Declared pawn = place_body(core, source.owner, 7);
    REQUIRE(int(core->scene_get_bodies(source.handle).size()) == 1);

    core->scene_replace_sources(
        target.owner,
        one_source(source.owner),
        NetwMultiplayer::SCENE_CHANGE_SCENE
    );

    CHECK(core->scene_subscribes(target.handle, 7));
    NETW_CHECK_EQ(int(core->scene_get_bodies(target.handle).size()), 0);
    CHECK(bool(pawn.owner->get_parent() == source.owner));

    source.owner->remove_child(pawn.owner);
    memdelete(pawn.owner);
    memdelete(source.owner);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] TL9 a source releases its players "
    "before the "
    "destination admits them, so a handler that spawns on arrival reads a "
    "roster the retiring scene has already left"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Declared source = declare_scene(core, root, "Source");
    const Declared target = declare_scene(core, root, "Target");
    adopt(core, 7);
    REQUIRE(core->scene_watch(source.handle, 7) == OK);

    const CallLog seen;
    core->scene_handle_of(source.handle)
        ->connect(StringName("viewer_exited"), seen.callable("left"));
    core->scene_handle_of(target.handle)
        ->connect(StringName("viewer_entered"), seen.callable("entered"));

    core->scene_replace_sources(
        target.owner,
        one_source(source.owner),
        NetwMultiplayer::SCENE_CHANGE_SCENE
    );

    const Vector<StringName> order = seen.order();
    REQUIRE(order.size() == 2);
    CHECK(bool(order[0] == StringName("left")));
    CHECK(bool(order[1] == StringName("entered")));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] TL10 a landed change resolves with "
    "the scene "
    "it landed in, and every player it moved watches that scene"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Declared source = declare_scene(core, root, "Source");
    const Declared target = declare_scene(core, root, "Target");
    adopt(core, 7);
    adopt(core, 9);
    REQUIRE(core->scene_watch(source.handle, 7) == OK);
    REQUIRE(core->scene_watch(target.handle, 9) == OK);

    const Ref<netw::NetwPromise> landed = core->scene_replace_sources(
        target.owner,
        one_source(source.owner),
        NetwMultiplayer::SCENE_CHANGE_SESSION
    );

    REQUIRE(landed.is_valid());
    CHECK(landed->get_is_completed());
    const Ref<netw::NetwScene> scene = landed->get_result();
    REQUIRE(scene.is_valid());
    CHECK(scene == core->scene_handle_of(target.handle));
    CHECK(core->scene_subscribes(target.handle, 7));
    CHECK(core->scene_subscribes(target.handle, 9));

    memdelete(root);
}

} // namespace TestNetwSceneTransitionLandingLaws
