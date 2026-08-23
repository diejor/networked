#pragma once

#include "netw/api/display_decl.hpp"
#include "netw/display_pump.hpp"
#include "netw/display_runtime.hpp"

namespace netw {

namespace display {

enum { FREEZE_MODE_KINEMATIC = 1 };

void apply_body_freeze(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    int p_role
);

void warn_self_feedback(const godot::Ref<NetwDisplayRuntime> &p_runtime);

int resolve_display_role(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const DisplayHooks &p_hooks
);

void resolve_role(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const DisplayHooks &p_hooks
);

} // namespace display

} // namespace netw
