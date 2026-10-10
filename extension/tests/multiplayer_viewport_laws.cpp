#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/api/context.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"

namespace TestMultiplayerViewportLaws {

using namespace godot;
using netw::MultiplayerViewport;
using netw::Netw;
using netw::NetwMultiplayer;

struct Mounted {
    MultiplayerViewport *tree = memnew(MultiplayerViewport);

    explicit Mounted(const char *p_name) {
        tree->set_name(p_name);
        netw::gd::scene_root()->add_child(tree);
    }

    ~Mounted() {
        netw::gd::scene_root()->remove_child(tree);
        memdelete(tree);
    }
};

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT1 a tree is the viewport its "
    "children live in and owns one session for its whole life, and mounting "
    "installs that session on its own branch"
) {
    Mounted branch("MT1Tree");
    const Ref<NetwMultiplayer> api = branch.tree->get_api();
    REQUIRE(api.is_valid());

    Node *child = memnew(Node);
    branch.tree->add_child(child);

    NETW_CHECK_EQ(int(child->get_viewport() == branch.tree), 1);
    NETW_CHECK_EQ(int(Netw::of(child) == api.ptr()), 1);
    NETW_CHECK_EQ(int(Netw::of(branch.tree) == api.ptr()), 1);
    NETW_CHECK_EQ(int(api->session_root() == branch.tree), 1);
    NETW_CHECK_EQ(int(branch.tree->get_api() == api), 1);

    branch.tree->remove_child(child);
    memdelete(child);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT2 two trees in one SceneTree "
    "are two viewports and two sessions, so a node answers the branch it sits "
    "under rather than the first session that happened to mount"
) {
    Mounted first("MT2TreeA");
    Mounted second("MT2TreeB");

    Node *under_first = memnew(Node);
    first.tree->add_child(under_first);
    Node *under_second = memnew(Node);
    second.tree->add_child(under_second);

    NETW_CHECK_EQ(int(first.tree->get_api() != second.tree->get_api()), 1);
    NETW_CHECK_EQ(int(under_first->get_viewport() == first.tree), 1);
    NETW_CHECK_EQ(int(under_second->get_viewport() == second.tree), 1);
    NETW_CHECK_EQ(int(Netw::of(under_first) == first.tree->get_api().ptr()), 1);
    NETW_CHECK_EQ(
        int(Netw::of(under_second) == second.tree->get_api().ptr()),
        1
    );

    first.tree->remove_child(under_first);
    memdelete(under_first);
    second.tree->remove_child(under_second);
    memdelete(under_second);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT7 a tree leaving the scene "
    "tree unregisters its path and clears its session's root path, so the "
    "branch answers the root session again"
) {
    MultiplayerViewport *tree = memnew(MultiplayerViewport);
    tree->set_name("MT7Tree");
    Node *root = netw::gd::scene_root();
    root->add_child(tree);
    const NodePath was = tree->get_path();
    const Ref<NetwMultiplayer> api = tree->get_api();
    REQUIRE(api.is_valid());

    root->remove_child(tree);

    SceneTree *scene = netw::gd::scene_tree();
    const Ref<MultiplayerAPI> at_old = scene->get_multiplayer(was);
    const Ref<MultiplayerAPI> at_root = scene->get_multiplayer();
    NETW_CHECK_EQ(int(at_old.ptr() == at_root.ptr()), 1);
    NETW_CHECK_EQ(int(api->session_get_inner()->get_root_path().is_empty()), 1);

    memdelete(tree);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT8 a renamed tree registers "
    "its new path, so a child still answers the tree's session and the old "
    "path answers the root session"
) {
    Mounted branch("MT8Tree");
    const Ref<NetwMultiplayer> api = branch.tree->get_api();
    REQUIRE(api.is_valid());
    Node *child = memnew(Node);
    branch.tree->add_child(child);
    const NodePath was = branch.tree->get_path();

    branch.tree->set_name("MT8Renamed");

    SceneTree *scene = netw::gd::scene_tree();
    const Ref<MultiplayerAPI> at_old = scene->get_multiplayer(was);
    const Ref<MultiplayerAPI> at_root = scene->get_multiplayer();
    const Ref<MultiplayerAPI> at_child
        = scene->get_multiplayer(child->get_path());
    NETW_CHECK_EQ(int(at_old.ptr() == at_root.ptr()), 1);
    NETW_CHECK_EQ(int(at_child.ptr() == api.ptr()), 1);
    NETW_CHECK_EQ(int(Netw::of(child) == api.ptr()), 1);
    NETW_CHECK_EQ(int(api->session_root() == branch.tree), 1);

    branch.tree->remove_child(child);
    memdelete(child);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT9 a tree owns its World2D and "
    "World3D, so its children never share physics or rendering with the "
    "window"
) {
    Mounted branch("MT9Tree");
    Window *window = netw::gd::scene_tree()->get_root();
    REQUIRE(window != nullptr);

    const Ref<World2D> own_2d = branch.tree->find_world_2d();
    const Ref<World3D> own_3d = branch.tree->find_world_3d();
    const Ref<World2D> window_2d = window->find_world_2d();
    const Ref<World3D> window_3d = window->find_world_3d();

    NETW_CHECK_EQ(int(branch.tree->is_using_own_world_3d()), 1);
    NETW_CHECK_EQ(int(own_2d.is_valid() && own_2d != window_2d), 1);
    NETW_CHECK_EQ(int(own_3d.is_valid() && own_3d != window_3d), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT12 a tree's session reads "
    "DECLARING once mounted and LIVE after its first poll"
) {
    Mounted branch("MT12Tree");
    const Ref<NetwMultiplayer> api = branch.tree->get_api();
    REQUIRE(api.is_valid());

    NETW_CHECK_EQ(
        int(api->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_DECLARING)
    );
    api->poll();
    NETW_CHECK_EQ(
        int(api->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT10 an api set before the tree "
    "enters is the one mounted on its branch, and setting one inside the "
    "tree is refused and keeps the mounted api"
) {
    MultiplayerViewport *tree = memnew(MultiplayerViewport);
    tree->set_name("MT10Tree");
    const Ref<NetwMultiplayer> chosen = NetwMultiplayer::create(Ref<Script>());
    REQUIRE(chosen.is_valid());
    tree->set_api(chosen);
    NETW_CHECK_EQ(int(tree->get_api() == chosen), 1);

    Node *root = netw::gd::scene_root();
    root->add_child(tree);
    SceneTree *scene = netw::gd::scene_tree();
    const Ref<MultiplayerAPI> mounted
        = scene->get_multiplayer(tree->get_path());
    NETW_CHECK_EQ(int(mounted.ptr() == chosen.ptr()), 1);

    const Ref<NetwMultiplayer> late = NetwMultiplayer::create(Ref<Script>());
    tree->set_api(late);
    NETW_CHECK_EQ(int(tree->get_api() == chosen), 1);
    const Ref<MultiplayerAPI> still = scene->get_multiplayer(tree->get_path());
    NETW_CHECK_EQ(int(still.ptr() == chosen.ptr()), 1);

    root->remove_child(tree);
    memdelete(tree);
    late->embed_dispose();
}

} // namespace TestMultiplayerViewportLaws
