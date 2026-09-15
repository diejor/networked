#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/rid.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/scene_core.hpp"
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
    return made;
}

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR1 a session presents nothing until a game "
    "names a scene, however many are live, so a peer that was never told what "
    "to look at looks at nothing"
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
    "[Networked][Scene][Hosted] SPR2 presentation names one scene and only "
    "the one it was given, so naming the second of two live scenes answers "
    "that one and never the one the live book answers first"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const Mounted annex = mount(core, root, StringName("Annex"));

    NETW_CHECK_EQ(int(core->scene_present(annex.scene)), int(OK));
    CHECK(core->scene_presented() == annex.scene);

    NETW_CHECK_EQ(int(core->scene_present(arena.scene)), int(OK));
    CHECK(core->scene_presented() == arena.scene);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR3 a session presents a scene it holds, so "
    "a handle it never went live with is refused and what it was presenting "
    "survives the refusal"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const RID stranger = core->get_liveness_core()->entity_create();
    REQUIRE(core->scene_present(arena.scene) == OK);

    NETW_CHECK_EQ(int(core->scene_present(stranger)), int(ERR_UNAVAILABLE));

    CHECK(core->scene_presented() == arena.scene);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR4 a scene leaving the book takes the "
    "presentation that named it with it and chooses no replacement, and a "
    "scene nobody was presenting leaves the standing choice alone"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const Mounted annex = mount(core, root, StringName("Annex"));
    REQUIRE(core->scene_present(arena.scene) == OK);

    core->scene_forget(annex.root);

    CHECK(core->scene_presented() == arena.scene);

    core->scene_forget(arena.root);

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 0);
    CHECK_FALSE(core->scene_presented().is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SPR5 the presentation edge names the scene "
    "left and the scene taken, a clear names nothing as the destination, and "
    "naming the scene already presented spends no edge at all"
) {
    const Ref<NetwMultiplayer> core = hosting();
    Node *root = memnew(Node);
    const Mounted arena = mount(core, root, StringName("Arena"));
    const Mounted annex = mount(core, root, StringName("Annex"));
    const CallLog moved;
    core->connect(
        StringName("scene_presentation_changed"),
        moved.callable("presented")
    );

    core->scene_present(arena.scene);
    core->scene_present(annex.scene);
    core->scene_present(annex.scene);
    core->scene_present(RID());

    NETW_CHECK_EQ(moved.count(StringName("presented")), 3);
    const Array taken = moved.args(StringName("presented"), 0);
    CHECK(Object::cast_to<Object>(taken[0]) == nullptr);
    CHECK(Object::cast_to<Object>(taken[1]) != nullptr);
    const Array cleared = moved.args(StringName("presented"), 2);
    CHECK(Object::cast_to<Object>(cleared[0]) != nullptr);
    CHECK(Object::cast_to<Object>(cleared[1]) == nullptr);

    memdelete(root);
}

} // namespace TestNetwScenePresentationLaws
