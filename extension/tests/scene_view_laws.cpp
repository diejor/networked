#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/scene_handle.hpp"
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

    const Ref<netw::NetwSceneHandle> named = api->scene_handle_of(scene);
    REQUIRE(named.is_valid());
    CHECK(named->get_is_declared());
    CHECK(named->get_entity() == scene);
    CHECK(named->get_label() == StringName("Arena"));

    const Ref<netw::NetwSceneHandle> again = api->scene_handle_of(scene);
    CHECK(again == named);

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] the view admits and releases a participant "
    "and reports what it admits, so a game never keys admission by peer id"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID scene = rig.declare_scene(StringName("Arena"));
    netw::NetwMultiplayer *api = rig.server();
    Node *container = api->entity_get_node(scene);
    REQUIRE(container != nullptr);
    rig.branch(-1)->add_child(container);
    rig.enter_scene(StringName("Arena"));

    const Ref<netw::NetwSceneHandle> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    api->participant_ensure(7);
    CHECK(api->participant_admit(7));
    const Ref<netw::NetwParticipant> joiner = api->participant_of(7);
    REQUIRE(joiner.is_valid());
    NETW_CHECK_EQ(int(joiner->get_peer_id()), 7);

    NETW_CHECK_EQ(int(view->admit(joiner)), int(OK));
    CHECK(view->admits(joiner));

    NETW_CHECK_EQ(int(view->release(joiner)), int(OK));
    CHECK_FALSE(view->admits(joiner));

    const Ref<netw::NetwParticipant> nobody;
    NETW_CHECK_EQ(int(view->admit(nobody)), int(ERR_INVALID_PARAMETER));
    CHECK_FALSE(view->admits(nobody));

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

    const Ref<netw::NetwSceneHandle> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    const CallLog heard;
    view->connect(StringName("participant_entered"), heard.callable("entered"));
    view->connect(StringName("participant_left"), heard.callable("left"));

    api->participant_ensure(7);
    CHECK(api->participant_admit(7));
    const Ref<netw::NetwParticipant> joiner = api->participant_of(7);
    REQUIRE(joiner.is_valid());
    NETW_CHECK_EQ(int(joiner->get_peer_id()), 7);

    NETW_CHECK_EQ(int(view->admit(joiner)), int(OK));
    NETW_CHECK_EQ(heard.count("entered"), 1);
    NETW_CHECK_EQ(heard.count("left"), 0);

    NETW_CHECK_EQ(int(view->release(joiner)), int(OK));
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

    const Ref<netw::NetwSceneHandle> view = api->scene_handle_of(scene);
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
    "participant rather than the peer id the flat dispatch carries, so the "
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

    const Ref<netw::NetwSceneHandle> view = api->scene_handle_of(scene);
    REQUIRE(view.is_valid());

    const CallLog heard;
    const Callable watcher = heard.answering("edge", false);
    view->observe(netw::NetwMultiplayer::SCENE_EVENT_PARTICIPANT, watcher);

    api->participant_ensure(7);
    CHECK(api->participant_admit(7));
    const Ref<netw::NetwParticipant> joiner = api->participant_of(7);
    REQUIRE(joiner.is_valid());
    NETW_CHECK_EQ(int(view->admit(joiner)), int(OK));

    NETW_CHECK_EQ(heard.count("edge"), 1);
    const Array carried = heard.args("edge");
    REQUIRE(carried.size() == 2);
    CHECK(bool(carried[0]));
    const Ref<netw::NetwParticipant> subject = carried[1];
    CHECK(subject == joiner);

    SUBCASE("unobserving the same callback stops the edges") {
        view->unobserve(
            netw::NetwMultiplayer::SCENE_EVENT_PARTICIPANT,
            watcher
        );
        NETW_CHECK_EQ(int(view->release(joiner)), int(OK));
        NETW_CHECK_EQ(heard.count("edge"), 1);
    }

    rig.branch(-1)->remove_child(container);
}

TEST_CASE(
    "[Networked][Scene] a node outside every scene answers a view "
    "that declares nothing rather than null, so a caller reads a fact "
    "instead of branching on absence"
) {
    LoopbackRig rig(1);
    Node *orphan = memnew(Node);
    const Ref<netw::NetwSceneHandle> view
        = netw::Netw::scene(orphan, StringName());
    CHECK(view.is_null());
    memdelete(orphan);
}

} // namespace TestSceneViewLaws

#endif
