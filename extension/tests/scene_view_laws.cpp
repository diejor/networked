#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/scene.hpp"
#include "support/joined_peer.h"
#include "support/netw_call_log.h"

namespace TestSceneViewLaws {

using namespace godot;
using namespace netw_test;

TEST_CASE(
    "[Networked][Scene] one scene answers one view, so a handle "
    "minted by name and a handle read off an entity inside it are the same "
    "object and a listener on either hears the other"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    rig.enter_scene(StringName("Arena"));

    const Ref<netw::NetwScene> named = api->scene_handle_of(scene);
    REQUIRE(named.is_valid());
    CHECK(named->get_root() == container);
    CHECK(named->get_rid() == scene);
    CHECK(named->get_label() == StringName("Arena"));

    const Ref<netw::NetwScene> again = api->scene_handle_of(scene);
    CHECK(again == named);

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] the view watches and unwatches a player "
    "and reports what it reaches, so a game never keys a watch by peer id"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    rig.enter_scene(StringName("Arena"));

    const Ref<netw::NetwScene> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    netw_test::seated_peer(api, 7);
    CHECK(api->player_has(7));
    const Ref<netw::NetwPlayer> joiner = api->player_of(7);
    REQUIRE(joiner.is_valid());
    NETW_CHECK_EQ(int(joiner->get_peer_id()), 7);

    NETW_CHECK_EQ(int(view->watch(joiner)), int(OK));
    CHECK(view->is_watching(joiner));

    NETW_CHECK_EQ(int(view->unwatch(joiner)), int(OK));
    CHECK_FALSE(view->is_watching(joiner));

    const Ref<netw::NetwPlayer> nobody;
    NETW_CHECK_EQ(int(view->watch(nobody)), int(ERR_INVALID_PARAMETER));
    CHECK_FALSE(view->is_watching(nobody));

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] the view announces its membership edges, so a "
    "game connects on the object it was handed instead of registering a raw "
    "observer twice and reading an entered flag"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    rig.enter_scene(StringName("Arena"));

    const Ref<netw::NetwScene> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    const CallLog heard;
    view->connect(StringName("viewer_entered"), heard.callable("entered"));
    view->connect(StringName("viewer_exited"), heard.callable("left"));

    netw_test::seated_peer(api, 7);
    CHECK(api->player_has(7));
    const Ref<netw::NetwPlayer> joiner = api->player_of(7);
    REQUIRE(joiner.is_valid());
    NETW_CHECK_EQ(int(joiner->get_peer_id()), 7);

    NETW_CHECK_EQ(int(view->watch(joiner)), int(OK));
    NETW_CHECK_EQ(heard.count("entered"), 1);
    NETW_CHECK_EQ(heard.count("left"), 0);

    NETW_CHECK_EQ(int(view->unwatch(joiner)), int(OK));
    NETW_CHECK_EQ(heard.count("entered"), 1);
    NETW_CHECK_EQ(heard.count("left"), 1);

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] the view answers the entities inside it as views "
    "rather than as the handles the scene book keys its own rows by"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    rig.enter_scene(StringName("Arena"));

    const Ref<netw::NetwScene> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    const TypedArray<netw::NetwEntity> inside = view->get_entities();
    NETW_CHECK_EQ(inside.size(), api->scene_get_entities(scene).size());
    for (int at = 0; at < inside.size(); ++at) {
        const Ref<netw::NetwEntity> held = inside[at];
        CHECK(held.is_valid());
    }

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] an observer the view registers is handed the "
    "player rather than the peer id the flat dispatch carries, so the "
    "internal never reaches the game's own callback signature"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    rig.enter_scene(StringName("Arena"));

    const Ref<netw::NetwScene> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    const CallLog heard;
    const Callable watcher = heard.answering("edge", false);
    view->observe(netw::NetwMultiplayer::SCENE_EVENT_VIEWER, watcher);

    netw_test::seated_peer(api, 7);
    CHECK(api->player_has(7));
    const Ref<netw::NetwPlayer> joiner = api->player_of(7);
    REQUIRE(joiner.is_valid());
    NETW_CHECK_EQ(int(view->watch(joiner)), int(OK));

    NETW_CHECK_EQ(heard.count("edge"), 1);
    const Array carried = heard.args("edge");
    REQUIRE(carried.size() == 2);
    CHECK(bool(carried[0]));
    const Ref<netw::NetwPlayer> subject = carried[1];
    CHECK(subject == joiner);

    SUBCASE("unobserving the same callback stops the edges") {
        view->unobserve(netw::NetwMultiplayer::SCENE_EVENT_VIEWER, watcher);
        NETW_CHECK_EQ(int(view->unwatch(joiner)), int(OK));
        NETW_CHECK_EQ(heard.count("edge"), 1);
    }

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] an entity answers the scene that contains it, and "
    "null off the tree or outside every scene"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    const Ref<netw::NetwScene> arena = api->scene_handle_of(scene);
    REQUIRE(arena.is_valid());

    Node *orphan = memnew(Node);
    const Ref<netw::NetwEntity> off_tree = netw::Netw::configure_entity(orphan);
    REQUIRE(off_tree.is_valid());
    CHECK(off_tree->get_scene().is_null());
    CHECK(netw::Netw::scene(orphan).is_null());

    Node *loose = memnew(Node);
    const Ref<netw::NetwEntity> outside = netw::Netw::configure_entity(loose);
    rig.branch(-1)->add_child(loose);
    REQUIRE(api->entity_of(loose).is_valid());
    CHECK(outside->get_scene().is_null());
    CHECK(netw::Netw::scene(loose).is_null());

    Node *member = memnew(Node);
    const Ref<netw::NetwEntity> inside = netw::Netw::configure_entity(member);
    container->add_child(member);
    REQUIRE(api->entity_of(member).is_valid());
    CHECK(inside->get_scene() == arena);
    CHECK(netw::Netw::scene(member) == arena);
    CHECK(netw::Netw::scene(container) == arena);

    container->remove_child(member);
    memdelete(member);
    rig.branch(-1)->remove_child(loose);
    memdelete(loose);
    memdelete(orphan);
    rig.branch(-1)->remove_child(container);
}

} // namespace TestSceneViewLaws

#endif
