#include "support/netw_test.h"

#include "support/fault_log.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

namespace TestEntityReadmissionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

constexpr int SERVER = -1;
constexpr int CLIENT = 0;
constexpr int SETTLE = 6;
constexpr int64_t LIVE = int64_t(netw::entity::Stage::LIVE);

const char *CRATE_SOURCE = R"(extends Node2D


func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_property(self, &"position").broadcast().heartbeat(60)
)";

Node *build_crate(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node2D *made
        = Object::cast_to<Node2D>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    return made;
}

Array crate_args() {
    Array out;
    out.push_back(String("Crate"));
    return out;
}

Array crate_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

struct CrateStand {
    LoopbackRig rig;
    Node2D *crate = nullptr;
    Ref<NetwEntity> entity;

    CrateStand() : rig(1) {
        rig.mount();
        flow_clocks(rig, 30);
        const Ref<Script> script = minted_script(CRATE_SOURCE);
        REQUIRE(script.is_valid());
        const int route = rig.spawn_registered(
            StringName("readmitted_crate"),
            callable_mp_static(&build_crate).bind(script),
            crate_args(),
            crate_types()
        );
        rig.step_ticks(SETTLE);
        crate = Object::cast_to<Node2D>(rig.route_node(route, SERVER));
        REQUIRE(crate != nullptr);
        entity = NetwEntity::of(crate);
        REQUIRE(entity.is_valid());
    }

    Node2D *copy_of(int64_t p_route) const {
        return Object::cast_to<Node2D>(rig.route_node(int(p_route), CLIENT));
    }

    bool live_at(int p_side, int64_t p_route) const {
        netw::NetwMultiplayer *session
            = p_side == SERVER ? rig.server() : rig.client(p_side);
        return session->liveness_route_state(p_route)
            == int64_t(netw::NetwMultiplayer::ENTITY_STATE_LIVE);
    }

    bool held_by_client(int64_t p_route) const {
        return live_at(CLIENT, p_route) && copy_of(p_route) != nullptr;
    }
};

TEST_CASE(
    "[Networked][Entity][SceneTree] a node the server re-adds after its "
    "departure settled comes back as a fresh spawn on every peer, carrying "
    "its configuration"
) {
    CrateStand stand;
    const int64_t first = stand.entity->get_route();
    const StringName id = stand.entity->get_name();
    Node *home = stand.crate->get_parent();
    home->remove_child(stand.crate);
    stand.rig.step_ticks(SETTLE);
    CHECK(stand.copy_of(first) == nullptr);

    const FaultWatch faults;
    home->add_child(stand.crate);
    stand.rig.step_ticks(SETTLE);
    const int64_t second = stand.entity->get_route();
    NETW_CHECK_EQ(faults.errors(), int64_t(0));
    const bool same = NetwEntity::of(stand.crate).ptr() == stand.entity.ptr();
    CHECK(same);
    NETW_CHECK_EQ(stand.entity->get_stage(), LIVE);
    const bool same_id = stand.entity->get_name() == id;
    CHECK(same_id);
    NETW_CHECK_GT(second, int64_t(0));
    CHECK(stand.held_by_client(second));
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a copy a client re-adds after its "
    "departure settled is admitted again on the route the server holds"
) {
    CrateStand stand;
    const int64_t route = stand.entity->get_route();
    Node2D *copy = stand.copy_of(route);
    REQUIRE(copy != nullptr);
    Node *home = copy->get_parent();
    home->remove_child(copy);
    stand.rig.step_ticks(SETTLE);

    const FaultWatch faults;
    home->add_child(copy);
    stand.rig.step_ticks(SETTLE);
    NETW_CHECK_EQ(faults.errors(), int64_t(0));
    NETW_CHECK_EQ(NetwEntity::of(copy)->get_stage(), LIVE);
    CHECK(stand.held_by_client(route));
    const bool same_copy = stand.copy_of(route) == copy;
    CHECK(same_copy);
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a node re-added under another session's "
    "branch is never booked by that session, and its own session books it "
    "again once it is back"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *node = memnew(Node);
    node->set_name("Wanderer");
    const Ref<NetwEntity> entity = NetwEntity::ensure(node);
    rig.branch(SERVER)->add_child(node);
    rig.step_ticks(SETTLE);
    rig.branch(SERVER)->remove_child(node);
    rig.step_ticks(SETTLE);

    rig.branch(CLIENT)->add_child(node);
    rig.step_ticks(SETTLE);
    CHECK(!rig.client(CLIENT)->entity_book_holds_under_test(node));
    CHECK(!rig.server()->entity_book_holds_under_test(node));

    rig.branch(CLIENT)->remove_child(node);
    rig.branch(SERVER)->add_child(node);
    rig.step_ticks(SETTLE);
    CHECK(rig.server()->entity_book_holds_under_test(node));
    const bool same = NetwEntity::of(node).ptr() == entity.ptr();
    CHECK(same);
    rig.branch(SERVER)->remove_child(node);
    memdelete(node);
}

} // namespace TestEntityReadmissionLaws

#endif
