#pragma once

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/sim/resolve.hpp"

namespace netw::sim {

struct AuthoredBody {
    godot::ObjectID node;
    godot::Variant freeze;
    godot::Variant freeze_mode;
};

struct Bodies {
    godot::LocalVector<AuthoredBody> held;
    godot::ObjectID visual;
    godot::Variant visual_transform;
    bool recorded = false;
    Mode applied = Mode::NONE;
};

struct HeldPose {
    godot::RID body;
    int dimension = 0;
    godot::Variant transform;
    godot::Variant linear;
    godot::Variant angular;
};

void hold_poses(
    const Bodies &p_bodies,
    const godot::RID &p_space,
    godot::LocalVector<HeldPose> &r_poses
);

void restore_poses(const godot::LocalVector<HeldPose> &p_poses);

bool is_solver_body(godot::Node *p_node);

void collect_bodies(
    godot::Node *p_owner,
    const godot::Vector<godot::NodePath> &p_declared,
    godot::LocalVector<godot::ObjectID> &r_bodies
);

void record_authored(
    Bodies &r_bodies,
    godot::Node *p_owner,
    const godot::Vector<godot::NodePath> &p_declared,
    const godot::NodePath &p_visual_root
);

void enter_proxy(const Bodies &p_bodies);

void restore_authored(const Bodies &p_bodies);

bool transition(Bodies &r_bodies, Mode p_mode);

void release(Bodies &r_bodies);

} // namespace netw::sim
