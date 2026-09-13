#include "support/netw_test.h"

#include "godot/camera.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/cameras.hpp"
#include "netw/api/nodes/view/host_scene_view.hpp"

namespace TestHostSceneViewLaws {

using namespace godot;
using netw::HostSceneView;
using netw::NetwMultiplayer;
using netw::ParticipantView;

struct Branch {
    SubViewport *viewport = nullptr;
    Node2D *player = nullptr;
    Node3D *level = nullptr;

    Branch() {
        viewport = memnew(SubViewport);
        viewport->set_size(Vector2i(320, 180));
        player = memnew(Node2D);
        player->set_name("Player");
        level = memnew(Node3D);
        level->set_name("Level");
        viewport->add_child(player);
        viewport->add_child(level);
        netw::gd::scene_root()->add_child(viewport);
    }

    Camera2D *flat_under(Node *p_parent) const {
        Camera2D *camera = memnew(Camera2D);
        p_parent->add_child(camera);
        return camera;
    }

    Camera3D *spatial_under(Node *p_parent) const {
        Camera3D *camera = memnew(Camera3D);
        p_parent->add_child(camera);
        return camera;
    }

    ~Branch() {
        netw::gd::scene_root()->remove_child(viewport);
        memdelete(viewport);
    }
};

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HV1 a camera search answers the "
    "branch root itself when it is one, the first match at any depth "
    "otherwise, and nothing at all for an empty branch or no branch"
) {
    Branch branch;

    NETW_CHECK_EQ(
        int(netw::view::first_camera(nullptr, "Camera2D") == nullptr),
        1
    );
    NETW_CHECK_EQ(
        int(netw::view::first_camera(branch.player, "Camera2D") == nullptr),
        1
    );

    Node *rig = memnew(Node2D);
    branch.player->add_child(rig);
    Camera2D *deep = branch.flat_under(rig);
    NETW_CHECK_EQ(
        int(netw::view::first_camera(branch.player, "Camera2D") == deep),
        1
    );
    NETW_CHECK_EQ(int(netw::view::first_camera(deep, "Camera2D") == deep), 1);
    NETW_CHECK_EQ(
        int(netw::view::first_camera(branch.player, "Camera3D") == nullptr),
        1
    );
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HV2 adoption prefers the player's "
    "own camera over the level's and 2D over 3D, so a player carrying "
    "neither falls back to the level and a world carrying neither adopts "
    "nothing rather than picking arbitrarily"
) {
    Branch bare;
    CHECK_FALSE(netw::view::adopt_camera(bare.player, bare.level));

    Camera3D *level_spatial = bare.spatial_under(bare.level);
    CHECK(netw::view::adopt_camera(bare.player, bare.level));
    CHECK(level_spatial->is_current());

    Camera2D *level_flat = bare.flat_under(bare.level);
    CHECK(netw::view::adopt_camera(bare.player, bare.level));
    NETW_CHECK_EQ(int(bare.viewport->get_camera_2d() == level_flat), 1);

    Camera2D *own = bare.flat_under(bare.player);
    CHECK(netw::view::adopt_camera(bare.player, bare.level));
    NETW_CHECK_EQ(int(bare.viewport->get_camera_2d() == own), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HV3 the session declines to mint a "
    "host view over one the author already placed, so a scene carrying its "
    "own view keeps it and only a bare root gets the stock one"
) {
    Node *root = memnew(Node);
    netw::gd::scene_root()->add_child(root);

    NETW_CHECK_EQ(
        int(NetwMultiplayer::scene_placed_view(nullptr) == nullptr),
        1
    );
    NETW_CHECK_EQ(int(NetwMultiplayer::scene_placed_view(root) == nullptr), 1);

    Node *scenery = memnew(Node);
    root->add_child(scenery);
    NETW_CHECK_EQ(int(NetwMultiplayer::scene_placed_view(root) == nullptr), 1);

    ParticipantView *placed = memnew(ParticipantView);
    root->add_child(placed);
    NETW_CHECK_EQ(int(NetwMultiplayer::scene_placed_view(root) == placed), 1);

    netw::gd::scene_root()->remove_child(root);
    memdelete(root);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HV4 a host view carries the stock "
    "presentation the moment it is minted, filling its parent rect, passing "
    "the mouse through to what is under it and taking the keyboard events "
    "nothing else handled"
) {
    Node *root = memnew(Node);
    netw::gd::scene_root()->add_child(root);
    HostSceneView *view = memnew(HostSceneView);
    root->add_child(view);

    CHECK(view->get_forwards_unhandled_input());
    NETW_CHECK_EQ(
        int(view->get_mouse_filter()),
        int(Control::MOUSE_FILTER_PASS)
    );
    CHECK_FALSE(view->get_suppressed());

    view->set_suppressed(true);
    CHECK_FALSE(view->is_visible());
    CHECK_FALSE(view->get_forwards_unhandled_input());
    NETW_CHECK_EQ(int(view->get_target() == nullptr), 1);

    view->set_suppressed(false);
    CHECK(view->is_visible());
    CHECK(view->get_forwards_unhandled_input());

    netw::gd::scene_root()->remove_child(root);
    memdelete(root);
}

} // namespace TestHostSceneViewLaws
