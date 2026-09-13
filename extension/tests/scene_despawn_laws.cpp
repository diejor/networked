#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSceneDespawn {

using namespace godot;
using netw::NetwEntityRecord;
using netw::NetwMultiplayer;

struct Mounted {
    RID scene;
    Node *container = nullptr;
    Node *level = nullptr;
};

Mounted mount(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    Mounted out;
    out.container = memnew(Node);
    p_parent->add_child(out.container);
    out.level = memnew(Node);
    out.level->set_name(p_stem);
    out.container->add_child(out.level);

    Ref<netw::NetwEntity> wrapper;
    wrapper.instantiate();
    NetwEntityRecord *const record = wrapper->get_record();
    out.scene = p_core->get_liveness_core()->entity_create();
    record->adopt_handle(out.scene);
    record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    p_core->liveness_bind(out.scene, route, wrapper, record, out.container);
    p_core->get_scene_core()->scene_enter(out.scene, p_stem, false);
    return out;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD1 a destroy takes the container out of "
    "the tree at once"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));

    CHECK(core->scene_destroy(arena.scene));

    NETW_CHECK_EQ(arena.container->get_parent(), nullptr);
    NETW_CHECK_EQ(root->get_child_count(), 0);
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD2 a retire opens the drain window and "
    "leaves the container mounted instead"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));

    core->get_scene_core()->scene_retire(arena.scene, 3);
    core->scene_settle_refresh();

    NETW_CHECK_EQ(arena.container->get_parent(), root);
    const Array retiring = core->get_scene_core()->retiring_scenes();
    NETW_CHECK_EQ(retiring.size(), 1);
    CHECK(RID(retiring[0]) == arena.scene);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD3 a destroy of no scene is refused and "
    "takes nothing down with it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));

    CHECK_FALSE(core->scene_destroy(RID()));

    NETW_CHECK_EQ(arena.container->get_parent(), root);
    NETW_CHECK_EQ(root->get_child_count(), 1);

    memdelete(root);
}

} // namespace TestNetwSceneDespawn
