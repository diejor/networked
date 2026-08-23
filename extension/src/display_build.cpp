#include "netw/display_build.hpp"

#include "godot/class_db.hpp"
#include "netw/display_channel.hpp"
#include "netw/api/display_decl.hpp"
#include "netw/display_history.hpp"
#include "netw/display_playhead.hpp"
#include "netw/display_port.hpp"
#include "netw/display_tracks.hpp"
#include "netw/api/display_spec_row.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace display {

static const char *GLOBAL_SPACE_CHANNELS[]
    = { "position", "rotation", "global_position", "global_rotation" };

static bool is_global_space_channel(const StringName &p_prop) {
    for (const char *name : GLOBAL_SPACE_CHANNELS) {
        if (p_prop == StringName(name)) {
            return true;
        }
    }
    return false;
}

bool is_spatial(const StringName &p_prop) {
    static const char *SPATIAL[] = { "rotation", "scale",     "transform",
                                     "quaternion", "basis",   "skew" };
    for (const char *name : SPATIAL) {
        if (p_prop == StringName(name)) {
            return true;
        }
    }
    return false;
}

StringName state_key_for(Node *p_node, const StringName &p_prop) {
    return StringName(
        String::num_int64(int64_t(p_node->get_instance_id())) + "/"
        + String(p_prop)
    );
}

static void warn_double_write(
    const Ref<NetwDisplayChannel> &p_standing,
    const Ref<NetwDisplayChannel> &p_fresh
) {
    NETW_WARN_COND(
        p_standing->get_target_obj() == p_fresh->get_target_obj()
            && p_standing->get_target_prop() == p_fresh->get_target_prop(),
        sys::INTERPOLATION,
        "two channels write '%s' on the same object, so the later one wins "
        "every frame and the earlier one is spent smoothing nothing",
        String(p_fresh->get_target_prop())
    );
}

Ref<NetwDisplayChannel> ensure_state(
    const Ref<NetwDisplayRuntime> &p_runtime,
    Node *p_node,
    const StringName &p_source_prop,
    const StringName &p_target_prop,
    const Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick,
    const DisplayHooks &p_hooks
) {
    if (p_spec.is_null() || p_spec->get_mode() == NetwInterpolate::MODE_NONE
        || p_node == nullptr) {
        return Ref<NetwDisplayChannel>();
    }
    const bool has_source = !p_source_prop.is_empty();

    Ref<NetwDisplayChannel> state;
    state.instantiate();
    state->set_name(p_target_prop);
    state->set_state_key(
        state_key_for(p_node, has_source ? p_source_prop : p_target_prop)
    );
    state->set_spec(p_spec);
    state->set_source_obj(has_source ? p_node : nullptr);
    state->set_source_prop(p_source_prop);
    state->set_target_prop(p_target_prop);
    state->set_authoring_ticks(p_authoring_tick);

    Node *owner = p_runtime->owner();
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    Node *visual = nullptr;
    if (owner != nullptr && config.is_valid()
        && !config->get_visual_root().is_empty()) {
        visual = owner->get_node_or_null(config->get_visual_root());
    }
    Node *target = (has_source && visual != nullptr) ? visual : p_node;
    state->set_target_obj(target);

    const Ref<NetwEntity> entity = p_runtime->entity();
    state->set_entity(entity.is_valid() ? entity->get_rid_handle() : RID());
    state->set_door(p_hooks.display_lane);

    Ref<NetwDisplayHistory> history;
    history.instantiate();
    history->set_mode(p_spec->get_mode());
    history->set_snap_distance(p_spec->get_snap_distance());
    state->set_history(history);

    const bool global_space = visual != nullptr && target == visual
        && is_global_space_channel(p_target_prop);

    Ref<NetwDisplayPort> port;
    port.instantiate();
    port->bind(target, owner);
    port->declare(p_target_prop, p_source_prop, global_space);
    state->set_port(port);

    NETW_WARN_COND(
        visual != nullptr && target == visual && !port->get_global_space()
            && !bool(visual->get("top_level")) && is_spatial(p_target_prop),
        sys::INTERPOLATION,
        "'%s' on a parented visual is written in LOCAL space and will be "
        "dragged by body writes; set top_level on the visual, or interpolate "
        "position and rotation instead",
        String(p_target_prop)
    );

    state->set_self_feedback(
        has_source && target == p_node && p_target_prop == p_source_prop
    );
    state->set_last_written(state->current_source_value());

    const Ref<NetwDisplayTracks> tracks = p_runtime->get_tracks();
    const int64_t declared = tracks->by_key(state->get_state_key());
    TypedArray<NetwDisplayChannel> states = p_runtime->get_states();
    if (declared >= 0) {
        const Ref<NetwDisplayChannel> standing = states[declared];
        standing->copy_shape_from(state);
        return standing;
    }
    const int64_t claimant = tracks->by_name(state->get_name());
    tracks->declare(state->get_state_key(), state->get_name());
    states.append(state);
    p_runtime->set_states(states);
    if (claimant >= 0) {
        warn_double_write(states[claimant], state);
    }
    return state;
}

void build_states(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Array &p_rows,
    const DisplayHooks &p_hooks
) {
    for (int at = 0; at < p_rows.size(); ++at) {
        const Ref<NetwDisplaySpecRow> row = p_rows[at];
        if (row.is_null() || !row->is_displayable()) {
            continue;
        }
        ensure_state(
            p_runtime,
            row->node_ptr(),
            row->get_source_prop(),
            row->get_target_prop(),
            row->get_spec(),
            false,
            p_hooks
        );
    }
}

bool wants_runtime(Node *p_owner, const DisplayHooks &p_hooks) {
    if (p_owner == nullptr) {
        return false;
    }
    if (p_owner->is_class("RigidBody2D") || p_owner->is_class("RigidBody3D")) {
        return true;
    }
    const Array rows = p_hooks.specs_of(p_owner);
    for (int at = 0; at < rows.size(); ++at) {
        const Ref<NetwDisplaySpecRow> row = rows[at];
        if (row.is_valid() && row->is_displayable()) {
            return true;
        }
    }
    return false;
}

void rebuild_runtime(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const DisplayHooks &p_hooks
) {
    if (p_runtime.is_null()) {
        return;
    }
    p_runtime->set_rebuild_queued(false);
    Node *owner = p_runtime->owner();
    if (p_runtime->entity().is_null() || owner == nullptr) {
        return;
    }
    p_runtime->set_states(TypedArray<NetwDisplayChannel>());
    p_runtime->get_tracks()->clear();
    p_runtime->get_playhead()->set_display_tick(-1);

    build_states(p_runtime, p_hooks.specs_of(owner), p_hooks);
    p_hooks.compute_sync_intervals(p_runtime);

    p_runtime->set_pump_mode(NetwDisplayDecl::PUMP_UNRESOLVED);
    p_hooks.resolve(p_runtime);
}

} // namespace display

} // namespace netw
