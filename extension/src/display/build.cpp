#include "netw/display/build.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/port.hpp"
#include "netw/display/roles.hpp"
#include "netw/display/spec_row.hpp"
#include "netw/display/tracks.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

static const char *GLOBAL_SPACE_CHANNELS[]
    = {"position", "rotation", "global_position", "global_rotation"};

static bool is_global_space_channel(const StringName &p_prop) {
    for (const char *name : GLOBAL_SPACE_CHANNELS) {
        if (p_prop == StringName(name)) {
            return true;
        }
    }
    return false;
}

bool is_spatial(const StringName &p_prop) {
    static const char *SPATIAL[]
        = {"rotation", "scale", "transform", "quaternion", "basis", "skew"};
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

static void warn_double_write(Channel *p_standing, Channel *p_fresh) {
    NETW_WARN_COND(
        p_standing->get_target_obj() == p_fresh->get_target_obj()
            && p_standing->get_target_prop() == p_fresh->get_target_prop(),
        sys::INTERPOLATION,
        "Two interpolations write '%s' on the same node, so only the later "
        "one shows. Interpolate each property once.",
        String(p_fresh->get_target_prop())
    );
}

Channel *ensure_state(
    Runtime *p_runtime,
    Node *p_node,
    const StringName &p_source_prop,
    const StringName &p_target_prop,
    const Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick,
    const Hooks &p_hooks
) {
    if (p_spec.is_null() || p_spec->get_mode() == NetwInterpolate::MODE_NONE
        || p_node == nullptr) {
        return nullptr;
    }
    const bool has_source = !p_source_prop.is_empty();

    Channel fresh;
    Channel *state = &fresh;
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
    const Decl &config = p_runtime->get_config();
    Node *visual = nullptr;
    if (owner != nullptr && !config.visual_root.is_empty()) {
        visual = owner->get_node_or_null(config.visual_root);
        NETW_ERR_COND_V(
            has_source && visual == nullptr,
            nullptr,
            sys::INTERPOLATION,
            "NetwDisplayHandle.visual_root '%s' is not a node under '%s', so "
            "'%s' is not interpolated, to keep it off the simulated node "
            "'%s'. Point visual_root at a child node.",
            String(config.visual_root),
            String(owner->get_name()),
            String(p_target_prop),
            String(p_node->get_name())
        );
    }
    const bool split = has_source && visual != nullptr;
    NETW_ERR_COND_V(
        has_source && !split && p_target_prop != p_source_prop
            && p_runtime->holds_body(p_node),
        nullptr,
        sys::INTERPOLATION,
        "'%s' on '%s' is not interpolated, because it follows '%s' but "
        "writes '%s' on the simulated body itself. Set "
        "NetwDisplayHandle.visual_root to draw it on a child node.",
        String(p_target_prop),
        String(p_node->get_name()),
        String(p_source_prop),
        String(p_target_prop)
    );
    Node *target = split ? visual : p_node;
    state->set_target_obj(target);

    const Ref<NetwEntity> entity = p_runtime->entity();
    state->set_entity(entity.is_valid() ? entity->get_rid_handle() : RID());
    state->set_door(p_hooks.display_lane);

    History &history = state->display_history();
    history.set_mode(p_spec->get_mode());
    history.set_snap_distance(p_spec->get_snap_distance());

    const bool global_space = split && is_global_space_channel(p_target_prop);

    Port &port = state->display_port();
    port.bind(target, owner);
    port.declare(p_target_prop, p_source_prop, global_space);

    NETW_WARN_COND(
        visual != nullptr && target == visual && !port.get_global_space()
            && !bool(visual->get("top_level")) && is_spatial(p_target_prop),
        sys::INTERPOLATION,
        "'%s' on the visual node is written in local space, so the body "
        "drags it. Set top_level on the visual node, or interpolate position "
        "and rotation.",
        String(p_target_prop)
    );

    state->set_self_feedback(
        has_source && target == p_node && p_target_prop == p_source_prop
    );
    state->set_last_written(state->current_source_value());

    Tracks &tracks = p_runtime->display_tracks();
    const int64_t declared = tracks.by_key(state->get_state_key());
    if (declared >= 0) {
        Channel *standing = p_runtime->channels()[declared];
        standing->copy_shape_from(fresh);
        return standing;
    }
    const int64_t claimant = tracks.by_name(state->get_name());
    tracks.declare(state->get_state_key(), state->get_name());
    Channel *added = p_runtime->add_channel();
    *added = fresh;
    if (claimant >= 0) {
        warn_double_write(p_runtime->channels()[claimant], added);
    }
    return added;
}

void build_states(
    Runtime *p_runtime,
    const LocalVector<SpecRow> &p_rows,
    const Hooks &p_hooks
) {
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        const SpecRow &row = p_rows[at];
        if (!row.is_displayable()) {
            continue;
        }
        ensure_state(
            p_runtime,
            row.node_ptr(),
            row.source_prop,
            row.target_prop,
            row.spec,
            false,
            p_hooks
        );
    }
}

bool wants_runtime(Node *p_owner, const Hooks &p_hooks) {
    if (p_owner == nullptr) {
        return false;
    }
    if (p_owner->is_class("RigidBody2D") || p_owner->is_class("RigidBody3D")) {
        return true;
    }
    const LocalVector<SpecRow> rows = p_hooks.specs_of(p_owner);
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at].is_displayable()) {
            return true;
        }
    }
    return false;
}

void rebuild_runtime(Runtime *p_runtime, const Hooks &p_hooks) {
    if (p_runtime == nullptr) {
        return;
    }
    p_runtime->set_rebuild_queued(false);
    Node *owner = p_runtime->owner();
    if (p_runtime->entity().is_null() || owner == nullptr) {
        return;
    }
    p_runtime->clear_channels();
    p_runtime->display_tracks().clear();
    p_runtime->display_playhead().set_display_tick(-1);

    {
        NETW_ZONE_NC("display build states", colors::INTERP);
        build_states(p_runtime, p_hooks.specs_of(owner), p_hooks);
    }
    {
        NETW_ZONE_NC("display sync intervals", colors::INTERP);
        p_hooks.compute_sync_intervals(p_runtime->entity_rid());
    }

    p_runtime->set_pump_mode(netw::display::PUMP_UNRESOLVED);
    NETW_ZONE_NC("display hooks resolve", colors::INTERP);
    p_hooks.resolve(p_runtime->entity_rid());
}

} // namespace netw::display
