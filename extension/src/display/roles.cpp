#include "netw/display/roles.hpp"

#include "netw/display/build.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/role_facts.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

bool owner_is_solver_body(Node *p_owner) {
    return p_owner != nullptr
        && (p_owner->is_class("RigidBody2D")
            || p_owner->is_class("RigidBody3D"));
}

void apply_body_freeze(Runtime *p_runtime, int p_role) {
    Node *owner = p_runtime->owner();
    if (!owner_is_solver_body(owner)) {
        return;
    }
    Dictionary saved = p_runtime->get_saved_freeze();
    if (p_role == netw::display::ROLE_REMOTE) {
        if (saved.is_empty()) {
            saved["freeze"] = owner->get("freeze");
            saved["mode"] = owner->get("freeze_mode");
            p_runtime->set_saved_freeze(saved);
        }
        owner->set("freeze_mode", FREEZE_MODE_KINEMATIC);
        owner->set("freeze", true);
        return;
    }
    if (saved.is_empty()) {
        return;
    }
    owner->set("freeze", saved["freeze"]);
    owner->set("freeze_mode", saved["mode"]);
    p_runtime->set_saved_freeze(Dictionary());
}

void warn_self_feedback(Runtime *p_runtime) {
    if (p_runtime->get_warned_self_feedback()) {
        return;
    }
    const LocalVector<Channel *> &states = p_runtime->channels();
    for (int at = 0; at < int(states.size()); ++at) {
        Channel *state = states[at];
        if (!state->get_self_feedback()) {
            continue;
        }
        p_runtime->set_warned_self_feedback(true);
        Node *owner = p_runtime->owner();
        NETW_WARN(
            sys::INTERPOLATION,
            "channel '%s' on '%s' interpolates a locally simulated property "
            "in place, so its smoothed output is skipped to keep it out of "
            "the control loop; redirect it with a .to() target or a "
            "visual_root",
            String(state->get_target_prop()),
            owner != nullptr ? String(owner->get_name()) : String("?")
        );
        return;
    }
}

namespace {

int role_verdict(Runtime *p_runtime, const Hooks &p_hooks) {
    const Decl &config = p_runtime->get_config();
    if (config.display_role != ROLE_AUTO) {
        return config.display_role;
    }
    const RID entity = p_runtime->entity_rid();
    return p_hooks.role_of(entity, p_hooks.authors(entity));
}

} // namespace

void resolve_role(Runtime *p_runtime, const Hooks &p_hooks) {
    const int role = role_verdict(p_runtime, p_hooks);
    const Decl &config = p_runtime->get_config();
    const int pump = config.pump_for(role);
    p_runtime->set_role(role);
    if (pump == p_runtime->get_pump_mode()) {
        return;
    }
    const int previous = int(p_runtime->get_pump_mode());
    const RID entity = p_runtime->entity_rid();
    p_runtime->set_display_offset_limit(p_hooks.clamp_for(entity));

    const LocalVector<Channel *> &states = p_runtime->channels();
    if (netw::display::pump_clears_history(previous, pump)) {
        for (int at = 0; at < int(states.size()); ++at) {
            Channel *state = states[at];
            state->display_history().clear();
        }
    }
    if (netw::display::pump_arms_offsets(previous, pump)) {
        for (int at = 0; at < int(states.size()); ++at) {
            Channel *state = states[at];
            state->render_offset().armed = !state->get_self_feedback();
        }
    }

    p_runtime->set_pump_mode(pump);
    retarget_drawn_node(p_runtime);
    apply_body_freeze(p_runtime, role);
    if (netw::display::pump_is_predicted(pump)) {
        warn_self_feedback(p_runtime);
    }
    if (pump == netw::display::PUMP_REMOTE) {
        p_hooks.compute_sync_intervals(entity);
    }
    p_hooks.chase_hook(entity, pump == netw::display::PUMP_CHASE);
    if (pump == netw::display::PUMP_DISABLED) {
        for (int at = 0; at < int(states.size()); ++at) {
            Channel *state = states[at];
            state->render_offset().clear();
        }
    }
}

} // namespace netw::display
