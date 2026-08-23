#pragma once

#include "godot/node.hpp"
#include "netw/display_channel.hpp"
#include "netw/display_pump.hpp"
#include "netw/display_runtime.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"

namespace netw {

namespace display {

godot::StringName state_key_for(
    godot::Node *p_node,
    const godot::StringName &p_prop
);

bool is_spatial(const godot::StringName &p_prop);

godot::Ref<NetwDisplayChannel> ensure_state(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    godot::Node *p_node,
    const godot::StringName &p_source_prop,
    const godot::StringName &p_target_prop,
    const godot::Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick,
    const DisplayHooks &p_hooks
);

void build_states(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Array &p_rows,
    const DisplayHooks &p_hooks
);

bool wants_runtime(godot::Node *p_owner, const DisplayHooks &p_hooks);

void rebuild_runtime(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const DisplayHooks &p_hooks
);

} // namespace display

} // namespace netw
