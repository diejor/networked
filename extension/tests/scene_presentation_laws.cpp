#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/rid.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/scene_core.hpp"
#include "netw/scene_membership.hpp"
#include "support/netw_call_log.h"

namespace TestNetwScenePresentationLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Mounted {
    Ref<netw::NetwEntity> wrapper;
    RID scene;
    Node *root = nullptr;
};

Mounted mount(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    Mounted made;
    made.root = memnew(Node);
    made.root->set_name(p_stem);
    made.root->set_meta(NetwMultiplayer::scene_container_meta(), true);
    p_parent->add_child(made.root);
    made.wrapper.instantiate();
    made.scene = p_core->get_liveness_core()->entity_create();
    made.wrapper->set_rid_handle(made.scene);
    made.wrapper->get_record()->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.scene,
        route,
        made.wrapper,
        made.wrapper->get_record(),
        made.root
    ));
    made.wrapper->set_owner(made.root);
    made.root->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    p_core->get_scene_core()->scene_enter(made.scene, p_stem, false);
    p_core->scene_player_display_invalidate();
    return made;
}

Node *stand(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    int64_t p_peer
) {
    Node *owner = memnew(Node);
    owner->set_name(StringName("Pawn"));
    p_parent->add_child(owner);
    Ref<netw::NetwEntity> wrapper;
    wrapper.instantiate();
    const RID handle = p_core->get_liveness_core()->entity_create();
    wrapper->set_rid_handle(handle);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        handle,
        route,
        wrapper,
        wrapper->get_record(),
        owner
    ));
    wrapper->set_owner(owner);
    wrapper->set_peer_id(p_peer);
    owner->set_meta(NetwMultiplayer::wrapper_meta(), wrapper);
    p_core->scene_player_display_invalidate();
    return owner;
}

void watch(
    const Ref<NetwMultiplayer> &p_core,
    const RID &p_scene,
    int64_t p_peer
) {
    p_core->membership_book().watch(p_peer, p_scene);
    p_core->scene_player_display_invalidate();
}

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR1 a peer that neither stands in a live "
    "scene nor watches one presents nothing, however many are live, so a "
    "session with no place for this peer in it draws nothing"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    mount(core, root, StringName("Arena"));
    mount(core, root, StringName("Annex"));

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 2);
    CHECK_FALSE(core->scene_presented().is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR2 a peer presents the one live scene its "
    "own body stands in, and the body crossing into the other takes the "
    "presentation with it rather than leaving it where it was"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const Mounted annex = mount(core, root, StringName("Annex"));

    Node *body = stand(core, arena.root, int64_t(core->get_unique_id()));
    CHECK(core->scene_presented() == arena.scene);

    arena.root->remove_child(body);
    annex.root->add_child(body);
    core->scene_player_display_invalidate();

    CHECK(core->scene_presented() == annex.scene);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR3 a body outranks a watch, so a peer "
    "watching one scene and standing in another presents the one it stands "
    "in and takes the watched one only once the body is gone"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const Mounted annex = mount(core, root, StringName("Annex"));
    const int64_t local = int64_t(core->get_unique_id());

    watch(core, annex.scene, local);
    CHECK(core->scene_presented() == annex.scene);

    Node *body = stand(core, arena.root, local);
    CHECK(core->scene_presented() == arena.scene);

    arena.root->remove_child(body);
    root->add_child(body);
    core->scene_player_display_invalidate();

    CHECK(core->scene_presented() == annex.scene);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR4 a peer reaching two live scenes at once "
    "presents neither, whether it stands in both or watches both, because a "
    "framework choosing between them would draw the wrong world in silence"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const Mounted annex = mount(core, root, StringName("Annex"));
    const int64_t local = int64_t(core->get_unique_id());

    watch(core, arena.scene, local);
    watch(core, annex.scene, local);
    CHECK_FALSE(core->scene_presented().is_valid());

    stand(core, arena.root, local);
    CHECK(core->scene_presented() == arena.scene);

    stand(core, annex.root, local);
    CHECK_FALSE(core->scene_presented().is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR5 a peer presents nothing for a body that "
    "is not its own and for a scene another peer watches, so what one peer "
    "reaches never decides what this one draws"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const int64_t stranger = int64_t(core->get_unique_id()) + 1;

    stand(core, arena.root, stranger);
    CHECK_FALSE(core->scene_presented().is_valid());

    watch(core, arena.scene, stranger);
    CHECK_FALSE(core->scene_presented().is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR6 the scene a peer stands in leaving the "
    "live book leaves it presenting nothing, so a peer whose world went away "
    "draws nothing rather than whatever is left standing"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    mount(core, root, StringName("Annex"));

    stand(core, arena.root, int64_t(core->get_unique_id()));
    CHECK(core->scene_presented() == arena.scene);

    core->scene_forget(arena.root);

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 1);
    CHECK_FALSE(core->scene_presented().is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR7 the presentation edge names the scene "
    "left and the scene taken, and a settle that reaches what the last one "
    "reached spends no edge at all"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const CallLog moved;
    core->connect(
        StringName("scene_presentation_changed"),
        moved.callable("presented")
    );

    Node *body = stand(core, arena.root, int64_t(core->get_unique_id()));
    NETW_CHECK_EQ(moved.count(StringName("presented")), 1);

    core->scene_player_display_invalidate();
    NETW_CHECK_EQ(moved.count(StringName("presented")), 1);

    arena.root->remove_child(body);
    root->add_child(body);
    core->scene_player_display_invalidate();

    NETW_CHECK_EQ(moved.count(StringName("presented")), 2);
    const Array taken = moved.args(StringName("presented"), 0);
    CHECK(Object::cast_to<Object>(taken[0]) == nullptr);
    CHECK(Object::cast_to<Object>(taken[1]) != nullptr);
    const Array cleared = moved.args(StringName("presented"), 1);
    CHECK(Object::cast_to<Object>(cleared[0]) != nullptr);
    CHECK(Object::cast_to<Object>(cleared[1]) == nullptr);

    memdelete(root);
}

} // namespace TestNetwScenePresentationLaws
