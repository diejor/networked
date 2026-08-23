#include "support/netw_test.h"

#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "godot/node.hpp"

namespace TestNetwSceneDespawn {

using namespace godot;
using netw::NetwEntityRecord;
using netw::NetwMultiplayerCore;

struct Mounted {
    RID scene;
    Node *container = nullptr;
    Node *level = nullptr;
};

Mounted mount(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    Mounted out;
    out.container = memnew(Node);
    p_parent->add_child(out.container);
    out.level = memnew(Node);
    out.level->set_name(p_stem);
    out.container->add_child(out.level);

    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    Ref<NetwEntityRecord> record;
    record.instantiate();
    out.scene = p_core->get_liveness_core()->entity_create();
    record->adopt_handle(out.scene);
    record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    p_core->liveness_bind(out.scene, route, wrapper, record, out.container);
    p_core->get_scene_core()->scene_enter(out.scene, p_stem, false);
    return out;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD1 a despawn with no linger takes the "
    "container out of the tree at once"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));

    NETW_CHECK_EQ(core->scene_despawn(arena.scene, 0), OK);

    NETW_CHECK_EQ(arena.container->get_parent(), nullptr);
    NETW_CHECK_EQ(root->get_child_count(), 0);
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD2 a lingering despawn leaves the container "
    "mounted and opens its drain window instead"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));

    NETW_CHECK_EQ(core->scene_despawn(arena.scene, 3), OK);

    NETW_CHECK_EQ(arena.container->get_parent(), root);
    const Array retiring = core->get_scene_core()->retiring_scenes();
    NETW_CHECK_EQ(retiring.size(), 1);
    CHECK(RID(retiring[0]) == arena.scene);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD3 a scene with nothing to despawn is "
    "refused by name and takes nothing down with it"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));

    NETW_CHECK_EQ(core->scene_despawn(RID(), 0), ERR_DOES_NOT_EXIST);

    Node *bare = memnew(Node);
    root->add_child(bare);
    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    Ref<NetwEntityRecord> record;
    record.instantiate();
    const RID hollow = core->get_liveness_core()->entity_create();
    record->adopt_handle(hollow);
    const int64_t route = core->get_liveness_core()->reserve_route();
    core->liveness_bind(hollow, route, wrapper, record, bare);

    NETW_CHECK_EQ(core->scene_despawn(hollow, 0), ERR_DOES_NOT_EXIST);
    NETW_CHECK_EQ(arena.container->get_parent(), root);
    NETW_CHECK_EQ(bare->get_parent(), root);

    memdelete(root);
}

} // namespace TestNetwSceneDespawn
