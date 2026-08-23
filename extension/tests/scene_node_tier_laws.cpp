#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneNodeTierLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::NetwSceneCore;
using netw_test::CallLog;

struct Mounted {
    Node *container = nullptr;
    Node *level = nullptr;
    Ref<netw::NetwEntity> entity;
    RID scene;
};

Mounted mount(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_container,
    const StringName &p_stem,
    bool p_owns_its_world
) {
    Mounted made;
    made.container = p_container;
    if (!String(p_stem).is_empty()) {
        made.level = memnew(Node);
        made.level->set_name(p_stem);
        made.container->add_child(made.level);
    }
    made.entity.instantiate();
    made.entity->attach_to(made.container);
    made.entity->set_scene_isolation(
        p_owns_its_world ? int64_t(NetwSceneCore::ISOLATION_OWN_WORLD)
                         : int64_t(NetwSceneCore::ISOLATION_NONE)
    );
    made.scene = p_core->get_liveness_core()->entity_create();
    made.entity->get_record()->adopt_handle(made.scene);
    REQUIRE(p_core->entity_of(made.container) == made.scene);
    p_core->get_scene_core()->scene_enter(made.scene, p_stem, p_owns_its_world);
    REQUIRE(p_core->scene_container(p_stem) == made.container);
    return made;
}

Mounted mount_plain(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    Node *container = memnew(Node);
    if (p_parent != nullptr) {
        p_parent->add_child(container);
    }
    return mount(p_core, container, p_stem, false);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT1 scene_level_of is the read half of "
    "scene_install_level, so a boundary with no content answers null rather "
    "than answering itself"
) {
    Node *container = memnew(Node);
    Node *level = memnew(Node);
    level->set_name(StringName("Arena"));

    NETW_CHECK_EQ(NetwMultiplayerCore::scene_level_of(container), nullptr);
    NETW_CHECK_EQ(NetwMultiplayerCore::scene_level_of(nullptr), nullptr);

    NetwMultiplayerCore::scene_install_level(container, level);

    NETW_CHECK_EQ(NetwMultiplayerCore::scene_level_of(container), level);
    CHECK(
        NetwMultiplayerCore::scene_level_of(container)->get_name()
        == StringName("Arena")
    );

    memdelete(container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT2 scene_containing answers the container "
    "the session MOUNTED, and answers null for a node in no scene at all"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    Node *deep = memnew(Node);
    arena.level->add_child(deep);

    NETW_CHECK_EQ(core->scene_containing(deep), arena.container);
    NETW_CHECK_EQ(core->scene_containing(arena.level), arena.container);
    NETW_CHECK_EQ(core->scene_containing(arena.container), arena.container);

    Node *outside = memnew(Node);
    root->add_child(outside);
    NETW_CHECK_EQ(core->scene_containing(outside), nullptr);
    NETW_CHECK_EQ(core->scene_containing(root), nullptr);
    NETW_CHECK_EQ(core->scene_containing(nullptr), nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT3 a scene the book stopped holding live is "
    "no longer what scene_containing answers, so a retired container stops "
    "standing in for its own subtree"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    NETW_CHECK_EQ(core->scene_containing(arena.level), arena.container);

    core->get_scene_core()->scene_exit(arena.scene);

    NETW_CHECK_EQ(core->scene_containing(arena.level), nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT4 freezing stops the CONTENT ROOT and "
    "leaves the boundary processing, because the boundary is what replicates"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    CHECK(core->scene_freeze(StringName("Arena")));

    NETW_CHECK_EQ(
        int(arena.level->get_process_mode()),
        int(Node::PROCESS_MODE_DISABLED)
    );
    NETW_CHECK_EQ(
        int(arena.container->get_process_mode()),
        int(Node::PROCESS_MODE_INHERIT)
    );

    CHECK_FALSE(core->scene_freeze(StringName("Annex")));

    const Mounted hollow = mount_plain(core, root, StringName());
    CHECK_FALSE(core->scene_freeze(StringName()));
    NETW_CHECK_EQ(
        int(hollow.container->get_process_mode()),
        int(Node::PROCESS_MODE_INHERIT)
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT5 activating a live stem forces its "
    "content root back to processing and answers the container already up, "
    "rather than building a second one"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    CHECK(core->scene_freeze(StringName("Arena")));
    NETW_CHECK_EQ(
        int(arena.level->get_process_mode()),
        int(Node::PROCESS_MODE_DISABLED)
    );

    NETW_CHECK_EQ(
        core->scene_activate_named(StringName("Arena")),
        arena.container
    );
    NETW_CHECK_EQ(
        int(arena.level->get_process_mode()),
        int(Node::PROCESS_MODE_INHERIT)
    );
    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 1);
    NETW_CHECK_EQ(root->get_child_count(), 1);

    NETW_CHECK_EQ(core->scene_activate_named(StringName("Annex")), nullptr);
    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT6 a declared stem already live is answered "
    "as it stands rather than spawned a second time, which is what makes the "
    "startup pass safe to run after a join has already brought it online"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    core->get_scene_core()->declaration_open(
        int(NetwSceneCore::ISOLATION_NONE),
        Callable()
    );
    core->get_scene_core()->declaration_row(
        StringName("Arena"),
        String("res://arena.tscn"),
        Variant(),
        true
    );
    core->get_scene_core()->declaration_row(
        StringName("Ghost"),
        String(),
        Variant(),
        true
    );
    core->get_scene_core()->declaration_publish();

    NETW_CHECK_EQ(
        core->scene_spawn_declared(StringName("Arena")),
        arena.container
    );
    NETW_CHECK_EQ(core->scene_spawn_declared(StringName("Ghost")), nullptr);
    NETW_CHECK_EQ(core->scene_spawn_declared(StringName("Annex")), nullptr);

    core->scene_spawn_initial();

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 1);
    NETW_CHECK_EQ(core->scene_container(StringName("Arena")), arena.container);
    NETW_CHECK_EQ(root->get_child_count(), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] SNT7 destroying takes the "
    "container out of the tree now, and retiring at the same zero window "
    "leaves it mounted, so the two take-downs are not one verb at two "
    "linger values"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Mounted annex = mount_plain(core, root, StringName("Annex"));

    CHECK(core->scene_retire_named(StringName("Arena"), 0));
    NETW_CHECK_EQ(arena.container->get_parent(), root);
    CHECK_FALSE(arena.container->is_queued_for_deletion());
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 1);

    CHECK(core->scene_destroy(StringName("Annex")));
    NETW_CHECK_EQ(annex.container->get_parent(), nullptr);
    CHECK(annex.container->is_queued_for_deletion());

    CHECK_FALSE(core->scene_destroy(StringName("Nowhere")));
    CHECK_FALSE(core->scene_retire_named(StringName("Nowhere"), 0));

    root->remove_child(arena.container);
    arena.container->queue_free();
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] SNT8 a drain window is counted in "
    "pumps, and the pump that closes it is the one that frees the container "
    "the record plane names"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const Mounted slow = mount(
        core,
        memnew(Node),
        StringName("Arena"),
        false
    );
    const Mounted quick = mount(
        core,
        memnew(Node),
        StringName("Annex"),
        false
    );

    CHECK(core->scene_retire_named(StringName("Arena"), 2));
    CHECK(core->scene_retire_named(StringName("Annex"), 1));
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 2);

    core->scene_pump_retired();

    CHECK(quick.container->is_queued_for_deletion());
    CHECK_FALSE(slow.container->is_queued_for_deletion());
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 1);

    core->scene_pump_retired();

    CHECK(slow.container->is_queued_for_deletion());
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT9 a scene edge takes the scene out of the "
    "live book and DEFERS the refresh to the settle, so a cascade of edges "
    "re-reads the presented scene once rather than once each"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog refreshed;
    core->set_scene_refresh(refreshed.callable("refresh"));
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Mounted annex = mount_plain(core, root, StringName("Annex"));

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 2);

    core->scene_forget(arena.container);
    CHECK(core->scene_retire_named(StringName("Annex"), 4));

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 0);
    NETW_CHECK_EQ(core->scene_container(StringName("Arena")), nullptr);
    NETW_CHECK_EQ(arena.container->get_parent(), root);
    NETW_CHECK_EQ(annex.container->get_parent(), root);
    NETW_CHECK_EQ(refreshed.count("refresh"), 0);

    core->settle_drain();

    NETW_CHECK_EQ(refreshed.count("refresh"), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT10 what a scene DECLARES about its world "
    "and what its container actually hosts are two readings, and the host "
    "view is owed the second one"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);

    CHECK_FALSE(core->scene_hosts_isolated_world());

    const Mounted declared = mount(
        core,
        memnew(Node),
        StringName("Arena"),
        true
    );
    root->add_child(declared.container);

    CHECK(core->scene_owns_its_world(declared.scene));
    CHECK_FALSE(core->scene_hosts_isolated_world());

    SubViewport *isolated = memnew(SubViewport);
    root->add_child(isolated);
    const Mounted hosted = mount(core, isolated, StringName("Annex"), true);

    CHECK(core->scene_owns_its_world(hosted.scene));
    CHECK(core->scene_hosts_isolated_world());

    CHECK_FALSE(core->scene_owns_its_world(RID()));

    memdelete(root);
}

} // namespace TestNetwSceneNodeTierLaws
