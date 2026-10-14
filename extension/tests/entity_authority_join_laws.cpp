#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/tree_hook.hpp"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

namespace TestNetwEntityAuthorityJoinLaws {

using namespace godot;
using netw::NetwEntity;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int FOREIGN = 5;

const char *CRATE_SOURCE = R"(extends Node3D


func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_property(self, &"position").broadcast().heartbeat(60)
)";

const char *HELD_CRATE_SOURCE = R"(extends Node3D


func _init() -> void:
	Netw.configure_entity(self).authority = NetwEntity.AUTHORITY_CONTROLLER
	Netw.configure_property(self, &"position").broadcast().heartbeat(60)
)";

Node *build_crate(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node3D *made
        = Object::cast_to<Node3D>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    return made;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Node *spawn_crate(
    LoopbackRig &p_rig,
    const char *p_id,
    const char *p_source = CRATE_SOURCE
) {
    const Ref<Script> script = netw_test::minted_script(p_source);
    REQUIRE(script.is_valid());
    const int route = p_rig.spawn_registered(
        StringName(p_id),
        callable_mp_static(&build_crate).bind(script),
        named("Crate"),
        one_type()
    );
    p_rig.step_ticks(4);
    Node *crate = p_rig.route_node(route, -1);
    REQUIRE(crate != nullptr);
    return crate;
}

Node *a_node(const char *p_name, int p_authority) {
    Node *made = memnew(Node);
    made->set_name(StringName(p_name));
    made->set_multiplayer_authority(p_authority, false);
    return made;
}

int watches_on(Node *p_node, const Ref<NetwEntity> &p_entity) {
    const uint64_t watcher = netw::tree::watched_by_under_test(p_node);
    return watcher == uint64_t(p_entity->get_instance_id()) ? 1 : 0;
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a node carrying a foreign authority that "
    "joins an AUTHORITY_SERVER entity is reset to the server, as a child "
    "and as a grandchild, whoever controls the entity"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    Node *crate = spawn_crate(rig, "authority_join_crate");
    SUBCASE("the server controls it") {
    }
    SUBCASE("a player controls it") {
        NetwEntity::of(crate)->set_controller(rig.peer_id(0));
        NETW_REQUIRE_EQ(
            NetwEntity::of(crate)->get_controller(),
            int64_t(rig.peer_id(0))
        );
    }
    NETW_REQUIRE_EQ(crate->get_multiplayer_authority(), 1);

    Node *child = a_node("Child", FOREIGN);
    crate->add_child(child);
    NETW_CHECK_EQ(child->get_multiplayer_authority(), 1);

    Node *grandchild = a_node("Grandchild", FOREIGN);
    child->add_child(grandchild);
    NETW_CHECK_EQ(grandchild->get_multiplayer_authority(), 1);

    Node *branch = a_node("Branch", FOREIGN);
    Node *leaf = a_node("Leaf", FOREIGN);
    branch->add_child(leaf);
    crate->add_child(branch);
    NETW_CHECK_EQ(branch->get_multiplayer_authority(), 1);
    NETW_CHECK_EQ(leaf->get_multiplayer_authority(), 1);
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a node joining a peer-controlled "
    "AUTHORITY_CONTROLLER entity takes the controller's authority, as a child "
    "and as a grandchild"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    Node *crate
        = spawn_crate(rig, "authority_join_held_crate", HELD_CRATE_SOURCE);
    const Ref<NetwEntity> entity = NetwEntity::of(crate);
    REQUIRE(entity.is_valid());
    const int peer = rig.peer_id(0);
    entity->set_controller(peer);
    NETW_REQUIRE_EQ(crate->get_multiplayer_authority(), peer);

    Node *child = a_node("Child", 1);
    crate->add_child(child);
    NETW_CHECK_EQ(child->get_multiplayer_authority(), peer);

    Node *grandchild = a_node("Grandchild", 1);
    child->add_child(grandchild);
    NETW_CHECK_EQ(grandchild->get_multiplayer_authority(), peer);
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a server-controlled entity holds no "
    "authority watch on its nodes, a peer-controlled AUTHORITY_CONTROLLER one "
    "watches every node, and returning control to the server leaves "
    "authority with the server"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    Node *crate
        = spawn_crate(rig, "authority_watch_crate", HELD_CRATE_SOURCE);
    const Ref<NetwEntity> entity = NetwEntity::of(crate);
    REQUIRE(entity.is_valid());
    Node *child = a_node("Child", 1);
    crate->add_child(child);
    NETW_CHECK_EQ(watches_on(crate, entity), 0);
    NETW_CHECK_EQ(watches_on(child, entity), 0);

    const int peer = rig.peer_id(0);
    entity->set_controller(peer);
    NETW_CHECK_EQ(watches_on(crate, entity), 1);
    NETW_CHECK_EQ(watches_on(child, entity), 1);

    entity->set_controller(1);
    NETW_CHECK_EQ(crate->get_multiplayer_authority(), 1);
    NETW_CHECK_EQ(child->get_multiplayer_authority(), 1);
    Node *late = a_node("Late", FOREIGN);
    child->add_child(late);
    NETW_CHECK_EQ(late->get_multiplayer_authority(), 1);
}

} // namespace TestNetwEntityAuthorityJoinLaws

#endif
