#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/scene.hpp"
#include "netw/api/session.hpp"
#include "support/minted_script.h"

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
    "[Networked][Scene] FDR1 a label answers the newest live scene under it, "
    "and a node always answers the scene that contains it, so two scenes "
    "under one label stay reachable through their own nodes"
) {
    LoopbackRig rig(0);
    rig.mount();
    const Ref<netw::NetwSession> session = rig.server()->get_session();

    Node *first = live_scene_named(rig, StringName("Arena"));

    NETW_CHECK_EQ(rig.server()->scene_find_all(StringName("Arena")).size(), 1);
    const Ref<netw::NetwScene> one = session->find_scene(StringName("Arena"));
    REQUIRE(one.is_valid());
    NETW_CHECK_EQ(one->get_root(), first);
    CHECK(one->get_entity() == netw::NetwEntity::of(first));
    const Ref<netw::NetwEntity> published = one->get("entity");
    CHECK(published == netw::NetwEntity::of(first));

    Node *child = memnew(Node);
    child->set_name("Child");
    const Ref<netw::NetwEntity> child_entity = Netw::configure_entity(child);
    first->add_child(child);
    REQUIRE(rig.server()->entity_of(child).is_valid());
    const Ref<netw::NetwScene> child_scene = child_entity->get_scene();
    REQUIRE(child_scene.is_valid());
    CHECK(child_scene->get_entity() == netw::NetwEntity::of(first));
    CHECK(child_scene->get_entity() != child_entity);
    CHECK(child_scene->get_entity()->get_scene() == child_scene);

    Node *second = live_scene_named(rig, StringName("Arena"));
    REQUIRE(second != first);

    NETW_CHECK_EQ(rig.server()->scene_find_all(StringName("Arena")).size(), 2);
    const Ref<netw::NetwScene> newest
        = session->find_scene(StringName("Arena"));
    REQUIRE(newest.is_valid());
    NETW_CHECK_EQ(newest->get_root(), second);

    const Ref<netw::NetwScene> held_first = Netw::scene(first);
    const Ref<netw::NetwScene> held_second = Netw::scene(second);
    REQUIRE(held_first.is_valid());
    REQUIRE(held_second.is_valid());
    NETW_CHECK_EQ(held_first->get_root(), first);
    NETW_CHECK_EQ(held_second->get_root(), second);

    retire(second);
    retire(first);
    CHECK(one->get_entity().is_null());
    CHECK(child_scene->get_entity().is_null());
}

TEST_CASE(
    "[Networked][Scene] SN1 one script gives each scene its own root "
    "name by default and shares an explicit label"
) {
    const Ref<Script> script = netw_test::minted_script(
        "extends Node\n"
        "func _init() -> void:\n"
        "\tname = \"Initial\"\n"
        "\tNetw.configure_scene(self)\n"
    );
    REQUIRE(script.is_valid());
    Node *a = Object::cast_to<Node>(script->call("new"));
    Node *b = Object::cast_to<Node>(script->call("new"));
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    a->set_name("A");
    b->set_name("B");
    StringName expected_a("A");
    StringName expected_b("B");
    SUBCASE("an explicit label applies to both roots") {
        Netw::configure_scene(a)->labeled("Shared");
        expected_a = StringName("Shared");
        expected_b = expected_a;
    }
    SUBCASE("the default is read from each root at hydration") {
    }
    const Ref<netw::NetwEntity> first = Netw::configure_entity(a);
    const Ref<netw::NetwEntity> second = Netw::configure_entity(b);
    first->get_record()->hydrate_identity(a);
    second->get_record()->hydrate_identity(b);
    CHECK(first->get_scene_label() == expected_a);
    CHECK(second->get_scene_label() == expected_b);
    CHECK(Netw::configure_scene(a)->get_label() == expected_a);
    CHECK(Netw::configure_scene(b)->get_label() == expected_b);
    memdelete(a);
    memdelete(b);
}

} // namespace TestSceneFrontDoorRigLaws

#endif
