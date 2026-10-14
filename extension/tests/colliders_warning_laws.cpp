#include "support/fault_log.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/value_flow_stand.h"

#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/lagcomp_core.hpp"

using namespace godot;

namespace TestNetwCollidersWarningLaws {

const char *GEARED_GLIDER = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var hitbox := Area2D.new()
	hitbox.name = &"Hitbox"
	visual.add_child(hitbox)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var glide := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(glide)
)";

Dictionary pose_row(double p_x) {
    Dictionary row;
    row[StringName("position")] = Vector3(real_t(p_x), 0.0, 0.0);
    return row;
}

void rewind_once(Node3D *p_body) {
    netw::NetwLagCompCore history;
    const int64_t slot = history.timeline_open(8);
    history.timeline_bind_owner(slot, p_body);
    Array keys;
    keys.push_back(StringName("position"));
    history.timeline_declare(slot, keys);
    history.timeline_record(slot, 1, pose_row(-5.0));
    PackedInt64Array slots;
    slots.push_back(slot);
    history.rewind(slots, 1, Callable());
}

template <typename T> T *seated(Node3D *p_parent) {
    T *body = memnew(T);
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    shape->set_shape(box);
    body->add_child(shape);
    p_parent->add_child(body);
    return body;
}

TEST_CASE(
    "[Networked][LagComp] K-CL2 a hitbox under the display's visual root "
    "warns that it moves with the drawn body"
) {
    const netw_test::FaultWatch faults;
    netw_test::LoopbackRig rig(1);
    rig.declare_world(netw_test::WorldDecl().clocked(30));
    rig.mount();
    const netw_test::FlowPair pair
        = netw_test::stand_flow_pair(rig, GEARED_GLIDER, "Glider");
    for (int tick = 0; tick < 6; ++tick) {
        netw::port_set(
            pair.authored,
            StringName("position"),
            Vector2(10.0 * tick, 0.0)
        );
        rig.step_ticks(1);
        netw::NetwNativeTests::display_pump(
            netw_test::flow_core(rig.client(0)),
            0.0
        );
    }
    NETW_CHECK_EQ(
        faults.warnings_containing("is under the visual root"),
        int64_t(1 + pair.mirrors.size())
    );
}

TEST_CASE(
    "[Networked][LagComp] K-CL3 a rewind of an AnimatableBody with "
    "sync_to_physics on warns that a restore cannot move it"
) {
    const netw_test::FaultWatch faults;
    Node3D *arena = memnew(Node3D);
    netw::gd::scene_root()->add_child(arena);
    AnimatableBody3D *platform = seated<AnimatableBody3D>(arena);
    platform->set_name("Platform");
    platform->set_sync_to_physics(true);
    rewind_once(platform);
    netw::gd::scene_root()->remove_child(arena);
    memdelete(arena);
    NETW_CHECK_EQ(
        faults.warnings_containing("has sync_to_physics on"),
        int64_t(1)
    );
}

TEST_CASE(
    "[Networked][LagComp] a rewind of a body whose only collider is "
    "kinematic warns on a build that cannot flush, and only there"
) {
    const netw_test::FaultWatch faults;
    Node3D *arena = memnew(Node3D);
    netw::gd::scene_root()->add_child(arena);
    CharacterBody3D *player = seated<CharacterBody3D>(arena);
    player->set_name("Player");
    rewind_once(player);
    netw::gd::scene_root()->remove_child(arena);
    memdelete(arena);
    NETW_CHECK_EQ(
        faults.warnings_containing("with no hitbox"),
        int64_t(netw::gd::can_flush_kinematic() ? 0 : 1)
    );
}

} // namespace TestNetwCollidersWarningLaws

#endif
