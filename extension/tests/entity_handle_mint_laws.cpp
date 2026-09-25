#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

namespace TestNetwEntityHandleMintLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwEntityRecord;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;

const char *CUBE_SOURCE = R"(extends Node3D


func _init() -> void:
	Netw.configure_property(self, &"position").broadcast().heartbeat(60)
)";

const char *ACTIVE_CUBE_SOURCE = R"(extends RigidBody3D


func _init() -> void:
	var entity := Netw.configure_entity(self)
	entity.transfer = NetwEntity.TRANSFER_IMMEDIATE
	entity.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	entity.simulation.claim_on_contact = true
	entity.simulation.release_on_rest = 1.0
	Netw.configure_property(self, &"position").broadcast().heartbeat(60)
)";

Node *build_cube(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node3D *made
        = Object::cast_to<Node3D>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    NetwEntity::ensure(made);
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

bool minted(Node *p_node, int64_t p_part) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    REQUIRE(entity.is_valid());
    return Ref<RefCounted>(entity->get_record()->minted_part(p_part))
        .is_valid();
}

TEST_CASE(
    "[Networked][Lifecycle][SceneTree] an entity that declares only a "
    "broadcast property owns no prediction handle and no simulation handle "
    "once it is mounted, on the server and on the client"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<Script> script = netw_test::minted_script(CUBE_SOURCE);
    REQUIRE(script.is_valid());
    const int route = rig.spawn_registered(
        StringName("mint_cube"),
        callable_mp_static(&build_cube).bind(script),
        named("Cube"),
        one_type()
    );
    rig.step_ticks(12);
    for (int side = -1; side < 1; ++side) {
        Node *cube = rig.route_node(route, side);
        REQUIRE(cube != nullptr);
        CHECK_FALSE(minted(cube, NetwEntityRecord::PART_PREDICTION));
        CHECK_FALSE(minted(cube, NetwEntityRecord::PART_SIMULATION));
    }
}

TEST_CASE(
    "[Networked][Lifecycle][SceneTree] an entity that declares its "
    "simulation and no prediction owns no prediction handle once it is "
    "mounted, on the server and on the client"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<Script> script = netw_test::minted_script(ACTIVE_CUBE_SOURCE);
    REQUIRE(script.is_valid());
    const int route = rig.spawn_registered(
        StringName("mint_active_cube"),
        callable_mp_static(&build_cube).bind(script),
        named("ActiveCube"),
        one_type()
    );
    rig.step_ticks(12);
    for (int side = -1; side < 1; ++side) {
        Node *cube = rig.route_node(route, side);
        REQUIRE(cube != nullptr);
        CHECK(minted(cube, NetwEntityRecord::PART_SIMULATION));
        CHECK_FALSE(minted(cube, NetwEntityRecord::PART_PREDICTION));
    }
}

} // namespace TestNetwEntityHandleMintLaws

#endif
