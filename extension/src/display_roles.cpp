#include "netw/display_roles.hpp"

#include "netw/display_channel.hpp"
#include "netw/display_history.hpp"
#include "netw/display_role_facts.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace display {

void apply_body_freeze(const Ref<NetwDisplayRuntime> &p_runtime, int p_role) {
    Node *owner = p_runtime->owner();
    if (owner == nullptr
        || !(owner->is_class("RigidBody2D") || owner->is_class("RigidBody3D"))) {
        return;
    }
    Dictionary saved = p_runtime->get_saved_freeze();
    if (p_role == NetwDisplayDecl::ROLE_REMOTE) {
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

void warn_self_feedback(const Ref<NetwDisplayRuntime> &p_runtime) {
    if (p_runtime->get_warned_self_feedback()) {
        return;
    }
    const TypedArray<NetwDisplayChannel> states = p_runtime->get_states();
    for (int at = 0; at < states.size(); ++at) {
        const Ref<NetwDisplayChannel> state = states[at];
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

int resolve_display_role(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const DisplayHooks &p_hooks
) {
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    if (config.is_valid()
        && config->get_display_role() != NetwDisplayDecl::ROLE_AUTO) {
        return config->get_display_role();
    }
    const Ref<NetwDisplayRoleFacts> facts
        = p_hooks.role_facts(p_runtime, p_hooks.authors(p_runtime));
    if (facts.is_null()) {
        return NetwDisplayDecl::ROLE_DISABLED;
    }
    Node *owner = p_runtime->owner();
    facts->set_owner_is_authority(
        owner != nullptr && owner->is_multiplayer_authority()
    );
    return facts->resolve();
}

void resolve_role(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const DisplayHooks &p_hooks
) {
    const int role = resolve_display_role(p_runtime, p_hooks);
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    const int pump = config.is_valid() ? config->pump_for(role)
                                       : int(NetwDisplayDecl::PUMP_DISABLED);
    p_runtime->set_role(role);
    if (pump == p_runtime->get_pump_mode()) {
        return;
    }
    const int previous = int(p_runtime->get_pump_mode());
    p_runtime->set_display_offset_limit(p_hooks.clamp_for(p_runtime));

    const TypedArray<NetwDisplayChannel> states = p_runtime->get_states();
    if (NetwDisplayDecl::pump_clears_history(previous, pump)) {
        for (int at = 0; at < states.size(); ++at) {
            const Ref<NetwDisplayChannel> state = states[at];
            state->get_history()->clear();
        }
    }
    if (NetwDisplayDecl::pump_arms_offsets(previous, pump)) {
        for (int at = 0; at < states.size(); ++at) {
            const Ref<NetwDisplayChannel> state = states[at];
            state->render_offset().armed = !state->get_self_feedback();
        }
    }

    p_runtime->set_pump_mode(pump);
    apply_body_freeze(p_runtime, role);
    if (NetwDisplayDecl::pump_is_predicted(pump)) {
        warn_self_feedback(p_runtime);
    }
    if (pump == NetwDisplayDecl::PUMP_REMOTE) {
        p_hooks.compute_sync_intervals(p_runtime);
    }
    p_hooks.chase_hook(p_runtime, pump == NetwDisplayDecl::PUMP_CHASE);
    if (pump == NetwDisplayDecl::PUMP_DISABLED) {
        for (int at = 0; at < states.size(); ++at) {
            const Ref<NetwDisplayChannel> state = states[at];
            state->render_offset().clear();
        }
    }
}

} // namespace display

} // namespace netw
