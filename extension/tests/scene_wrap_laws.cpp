#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/viewport.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/stretch_viewport_container.hpp"
#include "netw/api/scene.hpp"
#include "netw/scene_core.hpp"

namespace TestSceneWrapLaws {

using namespace godot;
using netw::Netw;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwSceneCore;
using netw::StretchViewportContainer;

struct Wrapped {
    Ref<NetwEntity> wrapper;
    RID handle;
    Node *root = nullptr;
    StretchViewportContainer *view = nullptr;
};

Ref<NetwEntity> bind(const Ref<NetwMultiplayer> &p_core, Node *p_node) {
    Ref<NetwEntity> wrapper;
    wrapper.instantiate();
    const RID handle = p_core->get_liveness_core()->entity_create();
    wrapper->set_rid_handle(handle);
    wrapper->stamp_multiplayer(p_core);
    REQUIRE(p_core->liveness_bind(
        handle,
        p_core->get_liveness_core()->reserve_route(),
        wrapper,
        wrapper->get_record(),
        p_node
    ));
    wrapper->set_owner(p_node);
    p_node->set_meta(NetwMultiplayer::wrapper_meta(), wrapper);
    return wrapper;
}

Wrapped wrapped(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    Wrapped made;
    made.root = memnew(Node);
    made.root->set_name(p_stem);
    made.wrapper = bind(p_core, made.root);
    made.handle = made.wrapper->get_rid_handle();
    made.wrapper->get_record()->set_declares_scene(true);
    made.wrapper->get_record()->set_scene_isolation(
        int64_t(NetwSceneCore::ISOLATION_OWN_WORLD)
    );
    p_parent->add_child(NetwMultiplayer::scene_wrap_world(made.root));
    made.view = NetwMultiplayer::scene_view_of(made.root);
    p_core->get_scene_core()->scene_enter(made.handle, p_stem, true);
    return made;
}

Node *stand(const Ref<NetwMultiplayer> &p_core, Node *p_scene_root) {
    Node *pawn = memnew(Node);
    pawn->set_name(StringName("Pawn"));
    p_scene_root->add_child(pawn);
    bind(p_core, pawn)->set_peer_id(p_core->get_unique_id());
    return pawn;
}

void settle(const Ref<NetwMultiplayer> &p_core) {
    p_core->scene_player_display_invalidate();
    p_core->scene_player_display_settle();
}

Ref<NetwMultiplayer> playing(NetwMultiplayer::Role p_role) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(p_role);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] W1 an isolated scene root is "
    "wrapped in a "
    "hidden StretchViewportContainer holding a SubViewport that owns both "
    "worlds, and neither wrapper carries an identity"
) {
    Node *arena = memnew(Node);
    arena->set_name("Arena");
    const Ref<NetwEntity> seat = NetwEntity::ensure(arena);
    REQUIRE(seat.is_valid());
    seat->set_declares_scene(true);
    seat->set_scene_isolation(int64_t(NetwSceneCore::ISOLATION_OWN_WORLD));

    REQUIRE(NetwMultiplayer::scene_root_is_isolated(arena));
    StretchViewportContainer *view = Object::cast_to<StretchViewportContainer>(
        NetwMultiplayer::scene_wrap_world(arena)
    );
    REQUIRE(view != nullptr);
    SubViewport *world = NetwMultiplayer::scene_world_of(arena);
    REQUIRE(world != nullptr);

    CHECK(bool(world->get_parent() == view));
    CHECK(bool(arena->get_parent() == world));
    CHECK_FALSE(view->is_visible());
    CHECK(world->is_using_own_world_3d());
    CHECK(world->get_world_2d().is_valid());
    NETW_CHECK_EQ(
        int(world->get_update_mode()),
        int(SubViewport::UPDATE_DISABLED)
    );
    CHECK(view->has_meta(NetwMultiplayer::scene_container_meta()));
    CHECK(world->has_meta(NetwMultiplayer::scene_container_meta()));
    CHECK(NetwEntity::of(view).is_null());
    CHECK(NetwEntity::of(world).is_null());
    CHECK(bool(view->get_name() == StringName("ArenaView")));
    CHECK(bool(world->get_name() == StringName("ArenaWorld")));

    CHECK(bool(NetwMultiplayer::scene_view_of(arena) == view));
    CHECK(bool(NetwMultiplayer::scene_outer_of(arena) == view));
    CHECK(bool(NetwMultiplayer::scene_inner_of(view) == arena));
    CHECK(bool(NetwMultiplayer::scene_inner_of(world) == arena));
    CHECK(bool(NetwMultiplayer::scene_wrap_world(arena) == view));
    NETW_CHECK_EQ(view->get_child_count(), 1);
    NETW_CHECK_EQ(world->get_child_count(), 1);

    memdelete(view);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] W2 retiring a wrapped scene frees "
    "its StretchViewportContainer, so no empty view is left behind"
) {
    const Ref<NetwMultiplayer> core
        = playing(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    Node *root = memnew(Node);
    const Wrapped arena = wrapped(core, root, StringName("Arena"));
    REQUIRE(arena.view != nullptr);

    NetwMultiplayer::scene_retire_world(arena.root);

    CHECK(arena.view->is_queued_for_deletion());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] W3 the node Netw.spawn answers for "
    "an isolated scene leads Netw.scene to that scene"
) {
    Node *branch = memnew(Node);
    branch->set_name(StringName("WrapBranch"));
    netw::gd::scene_root()->add_child(branch);
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(branch->get_path());
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_inner(inner);
    netw::gd::scene_tree()->set_multiplayer(core, branch->get_path());

    const Wrapped arena = wrapped(core, branch, StringName("Arena"));
    Node *answered = NetwMultiplayer::scene_outer_of(arena.root);

    const Ref<netw::NetwScene> scene = Netw::scene(answered);
    CHECK(scene.is_valid());
    CHECK(bool(scene.is_valid() && scene->get_root() == arena.root));

    netw::gd::scene_tree()->set_multiplayer(
        Ref<MultiplayerAPI>(),
        branch->get_path()
    );
    branch->get_parent()->remove_child(branch);
    memdelete(branch);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] W4 on a listen host the view of the "
    "scene its "
    "player stands in is visible and every other view is hidden, and both "
    "turn over when the player moves"
) {
    const Ref<NetwMultiplayer> core
        = playing(NetwMultiplayer::ROLE_LISTEN_SERVER);
    Node *root = memnew(Node);
    const Wrapped arena = wrapped(core, root, StringName("Arena"));
    const Wrapped annex = wrapped(core, root, StringName("Annex"));
    Node *pawn = stand(core, annex.root);
    settle(core);

    CHECK(annex.view->is_visible());
    CHECK_FALSE(arena.view->is_visible());

    annex.root->remove_child(pawn);
    arena.root->add_child(pawn);
    settle(core);

    CHECK(arena.view->is_visible());
    CHECK_FALSE(annex.view->is_visible());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] W5 with no player standing anywhere "
    "the one "
    "live scene is shown, and a second live scene leaves neither shown"
) {
    const Ref<NetwMultiplayer> core
        = playing(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    Node *root = memnew(Node);
    const Wrapped arena = wrapped(core, root, StringName("Arena"));
    settle(core);

    CHECK(arena.view->is_visible());

    const Wrapped annex = wrapped(core, root, StringName("Annex"));
    settle(core);

    CHECK_FALSE(arena.view->is_visible());
    CHECK_FALSE(annex.view->is_visible());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] W6 a view the game shows itself "
    "stays shown "
    "through a settle that does not change the shown scene"
) {
    const Ref<NetwMultiplayer> core
        = playing(NetwMultiplayer::ROLE_LISTEN_SERVER);
    Node *root = memnew(Node);
    const Wrapped arena = wrapped(core, root, StringName("Arena"));
    const Wrapped annex = wrapped(core, root, StringName("Annex"));
    stand(core, arena.root);
    settle(core);
    REQUIRE_FALSE(annex.view->is_visible());

    annex.view->show();
    settle(core);

    CHECK(annex.view->is_visible());
    CHECK(arena.view->is_visible());

    memdelete(root);
}

} // namespace TestSceneWrapLaws
