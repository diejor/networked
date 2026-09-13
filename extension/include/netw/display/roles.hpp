#pragma once

#include "netw/display/decl.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/runtime.hpp"

namespace netw::display {

enum { FREEZE_MODE_KINEMATIC = 1 };

bool owner_is_solver_body(godot::Node *p_owner);

void apply_body_freeze(Runtime *p_runtime, int p_role);

void warn_self_feedback(Runtime *p_runtime);

void resolve_role(Runtime *p_runtime, const Hooks &p_hooks);

} // namespace netw::display
