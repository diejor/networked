#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/scene_handle.hpp"

namespace TestSceneFrontDoorRigLaws {

using namespace godot;
using namespace netw_test;
using netw::Netw;
using netw::NetwMultiplayer;

Node *live_scene_named(LoopbackRig &p_rig, const StringName &p_stem) {
    NetwMultiplayer *api = p_rig.server();
    Node *root = memnew(Node);
    root->set_name(p_stem);
    p_rig.branch()->add_child(root);

    const RID scene = api->entity_create();
    REQUIRE(scene.is_valid());
    NETW_CHECK_EQ(int(api->scene_declare(scene)), int(godot::OK));
    NETW_CHECK_GT(int(api->entity_admit(scene)), 0);
    NETW_CHECK_EQ(int(api->entity_bind_node(scene, root)), int(godot::OK));
    api->scene_set_param(scene, NetwMultiplayer::SCENE_PARAM_LABEL, p_stem);
    api->scene_root_online(root);
    p_rig.pump();
    return root;
}

void retire(Node *p_root) {
    if (p_root != nullptr) {
        p_root->get_parent()->remove_child(p_root);
        memdelete(p_root);
    }
}

TEST_CASE(
    "[Networked][Scene] FDR1 a label names ONE live scene, so a second scene "
    "under the same label makes the label answer nothing rather than "
    "whichever instance the book happens to walk first"
) {
    LoopbackRig rig(0);
    rig.mount();

    Node *first = live_scene_named(rig, StringName("Arena"));

    NETW_CHECK_EQ(rig.server()->scene_find_all(StringName("Arena")).size(), 1);
    const Ref<netw::NetwSceneHandle> one
        = Netw::scene(first, StringName("Arena"));
    REQUIRE(one.is_valid());
    NETW_CHECK_EQ(one->get_root(), first);

    Node *second = live_scene_named(rig, StringName("Arena"));
    REQUIRE(second != first);

    NETW_CHECK_EQ(rig.server()->scene_find_all(StringName("Arena")).size(), 2);
    CHECK(Netw::scene(first, StringName("Arena")).is_null());

    const Ref<netw::NetwSceneHandle> held_first
        = Netw::scene(first, StringName());
    const Ref<netw::NetwSceneHandle> held_second
        = Netw::scene(second, StringName());
    REQUIRE(held_first.is_valid());
    REQUIRE(held_second.is_valid());
    NETW_CHECK_EQ(held_first->get_root(), first);
    NETW_CHECK_EQ(held_second->get_root(), second);

    retire(second);
    retire(first);
}

} // namespace TestSceneFrontDoorRigLaws

#endif
