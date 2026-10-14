#pragma once

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw::colliders {

struct ServerPose {
    godot::ObjectID body;
    godot::Variant pose;
};

void teleport(godot::Object *p_owner);

void settle(godot::Object *p_owner);

void report_unseen(godot::Object *p_owner);

void exclude_visual_root(godot::Node *p_owner, godot::Node *p_visual);

enum class Fault : uint8_t {
    SYNC_TO_PHYSICS,
    UNSEEN_KINEMATIC,
    VISUAL_ROOT,
    WRITE_INSIDE_REWIND,
    STEPPED_KINEMATIC,
    STEPPED_NO_STEP,
};

bool first_report(const godot::Object *p_subject, Fault p_fault);

godot::Node *first_kinematic_body(godot::Object *p_owner);

void hold_server_poses(
    godot::Object *p_owner,
    godot::LocalVector<ServerPose> &r_held
);

void restore_back(
    godot::Object *p_owner,
    const godot::LocalVector<ServerPose> &p_held
);

} // namespace netw::colliders
