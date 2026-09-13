#pragma once

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/runtime.hpp"

namespace netw::display {

godot::StringName state_key_for(
    godot::Node *p_node,
    const godot::StringName &p_prop
);

bool is_spatial(const godot::StringName &p_prop);

bool draws_on_owner(Runtime *p_runtime);

void retarget_drawn_node(Runtime *p_runtime);

Channel *ensure_state(
    Runtime *p_runtime,
    godot::Node *p_node,
    const godot::StringName &p_source_prop,
    const godot::StringName &p_target_prop,
    const godot::Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick,
    const Hooks &p_hooks
);

void build_states(
    Runtime *p_runtime,
    const godot::LocalVector<SpecRow> &p_rows,
    const Hooks &p_hooks
);

bool wants_runtime(godot::Node *p_owner, const Hooks &p_hooks);

void rebuild_runtime(Runtime *p_runtime, const Hooks &p_hooks);

} // namespace netw::display
