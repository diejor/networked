#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/action.hpp"
#include "netw/api/netw_multiplayer.hpp"

#if defined(NETW_TIER_HOSTED)
#include <godot_cpp/classes/node2d.hpp>
#endif

namespace netw_test {

#if defined(NETW_TIER_HOSTED)

class Carrier : public godot::Node2D {
    GDCLASS(Carrier, godot::Node2D)

    godot::HashMap<godot::StringName, godot::Variant> properties;
    godot::Vector<godot::StringName> property_order;
    int fresh_effect_count = 0;
    int interpolation_resets = 0;
    double rewound_x = 0.0;
    int rewind_visits = 0;
    bool count_replay_effects = false;
    double last_simulated_x = 0.0;
    double carry_gain = 1.0;
    godot::Ref<netw::NetwAction> action;
    int ghost_count = 0;
    int confirmed_count = 0;
    int denied_count = 0;
    int server_requests = 0;
    int64_t last_view_tick = -1;
    int64_t last_requested_tick = -1;
    int64_t last_execution_tick = -1;
    bool denies = true;

protected:
    static void _bind_methods() {
        godot::ClassDB::bind_method(
            D_METHOD("_network_tick", "delta", "tick", "fresh"),
            &Carrier::network_tick
        );
        godot::ClassDB::bind_method(
            D_METHOD("observe_self"),
            &Carrier::observe_self
        );
        godot::ClassDB::bind_method(
            D_METHOD("sum_into_speed", "delta", "tick", "fresh"),
            &Carrier::sum_into_speed
        );
        godot::ClassDB::bind_method(
            D_METHOD("carry_speed", "value", "ctx"),
            &Carrier::carry_speed
        );
        godot::ClassDB::bind_method(
            D_METHOD("carry_speed_and_write", "value", "ctx"),
            &Carrier::carry_speed_and_write
        );
        godot::ClassDB::bind_method(
            D_METHOD("carry_position", "value", "ctx"),
            &Carrier::carry_position
        );
        godot::ClassDB::bind_method(
            D_METHOD("_predict_ghost"),
            &Carrier::predict_ghost
        );
        godot::ClassDB::bind_method(
            D_METHOD("_server_action", "ctx"),
            &Carrier::server_action
        );
        godot::ClassDB::bind_method(
            D_METHOD("_note_confirmed"),
            &Carrier::note_confirmed
        );
        godot::ClassDB::bind_method(
            D_METHOD("_note_denied"),
            &Carrier::note_denied
        );
    }

    bool _set(const godot::StringName &p_name, const godot::Variant &p_value) {
        const auto found = properties.find(p_name);
        if (found == properties.end()) {
            return false;
        }
        properties[p_name] = p_value;
        return true;
    }

    bool _get(const godot::StringName &p_name, godot::Variant &r_value) const {
        const auto found = properties.find(p_name);
        if (found == properties.end()) {
            return false;
        }
        r_value = found->value;
        return true;
    }

    void _get_property_list(godot::List<godot::PropertyInfo> *p_list) const {
        for (const godot::StringName &name : property_order) {
            const godot::Variant value = properties[name];
            p_list->push_back(godot::PropertyInfo(value.get_type(), name));
        }
    }

public:
    void _notification(int p_what) {
        if (p_what == godot::Node::NOTIFICATION_RESET_PHYSICS_INTERPOLATION) {
            interpolation_resets += 1;
        }
    }

    int reset_count() const {
        return interpolation_resets;
    }

    void define(
        const godot::StringName &p_name,
        const godot::Variant &p_value
    ) {
        if (!properties.has(p_name)) {
            property_order.push_back(p_name);
        }
        properties[p_name] = p_value;
        notify_property_list_changed();
    }

    void observe_self() {
        rewound_x = get_position().x;
        rewind_visits += 1;
    }

    double observed_x() const {
        return rewound_x;
    }

    int rewind_visit_count() const {
        return rewind_visits;
    }

    void sum_into_speed(double, int64_t, bool) {
        set("speed", double(get("speed")) + double(get("throttle")));
    }

    godot::Variant carry_speed(
        const godot::Variant &p_value,
        godot::Object *p_ctx
    ) {
        const godot::Dictionary input = p_ctx->get("input");
        return double(p_value)
            + double(input.get("throttle", 0.0)) * carry_gain;
    }

    godot::Variant carry_speed_and_write(
        const godot::Variant &p_value,
        godot::Object *p_ctx
    ) {
        const godot::Variant out = carry_speed(p_value, p_ctx);
        set("speed", double(get("speed")) + 1.0);
        return out;
    }

    godot::Variant carry_position(
        const godot::Variant &p_value,
        godot::Object *p_ctx
    ) {
        const godot::Dictionary input = p_ctx->get("input");
        const godot::Variant motion
            = input.get(godot::StringName("motion"), godot::Vector2());
        return godot::Vector2(p_value)
            + godot::Vector2(motion)
            * godot::real_t(60.0 * double(p_ctx->get("delta")) * carry_gain);
    }

    godot::Node *predict_ghost() {
        godot::Node *ghost = memnew(godot::Node);
        ghost->set_name("Ghost");
        add_child(ghost);
        ghost_count += 1;
        return ghost;
    }

    void server_action(netw::NetwActionContext *p_ctx) {
        REQUIRE_MESSAGE(p_ctx != nullptr, "an action needs a context");
        server_requests += 1;
        last_view_tick = p_ctx->get_view_tick();
        last_requested_tick = p_ctx->get_requested_tick();
        last_execution_tick = p_ctx->get_execution_tick();
        if (denies) {
            p_ctx->deny();
        }
    }

    void note_confirmed() {
        confirmed_count += 1;
    }

    void note_denied() {
        denied_count += 1;
    }

    void arm_action(netw::NetwMultiplayer *p_api) {
        REQUIRE_MESSAGE(p_api != nullptr, "an action needs a session");
        action = p_api->lagcomp_action(
            godot::Callable(this, godot::StringName("_server_action"))
        );
        REQUIRE_MESSAGE(action.is_valid(), "the session minted no action");
        if (action.is_null()) {
            return;
        }
        action->set_predict(
            godot::Callable(this, godot::StringName("_predict_ghost"))
        );
        action->connect(
            "confirmed",
            godot::Callable(this, godot::StringName("_note_confirmed"))
        );
        action->connect(
            "denied",
            godot::Callable(this, godot::StringName("_note_denied"))
        );
    }

    godot::Ref<netw::NetwAction> armed_action() const {
        return action;
    }

    void fire(int64_t p_view_tick, int p_timing_mode) {
        REQUIRE_MESSAGE(action.is_valid(), "a fire needs an armed action");
        if (action.is_null()) {
            return;
        }
        action->set_timing_mode(netw::NetwAction::TimingMode(p_timing_mode));
        action->request(p_view_tick, godot::Variant());
    }

    godot::Node *ghost() const {
        return get_node_or_null(godot::NodePath("Ghost"));
    }

    int ghosts() const {
        return ghost_count;
    }

    int confirmations() const {
        return confirmed_count;
    }

    int denials() const {
        return denied_count;
    }

    int requests() const {
        return server_requests;
    }

    int64_t viewed_tick() const {
        return last_view_tick;
    }

    int64_t requested_tick() const {
        return last_requested_tick;
    }

    int64_t executed_tick() const {
        return last_execution_tick;
    }

    void set_carry_gain(double p_gain) {
        carry_gain = p_gain;
    }

    void network_tick(double p_delta, int64_t, bool p_fresh) {
        const godot::Variant value = get("motion");
        if (value.get_type() != godot::Variant::VECTOR2) {
            return;
        }
        set_position(
            get_position()
            + godot::Vector2(value) * godot::real_t(60.0 * p_delta)
        );
        last_simulated_x = get_position().x;
        if (p_fresh && bool(get("bombing"))) {
            fresh_effect_count += 1;
        }
        if (!p_fresh && count_replay_effects && bool(get("bombing"))) {
            fresh_effect_count += 1;
        }
    }

    int fresh_effects() const {
        return fresh_effect_count;
    }

    double simulated_x() const {
        return last_simulated_x;
    }

    void replay_effects(bool p_enabled) {
        count_replay_effects = p_enabled;
    }
};

#endif

} // namespace netw_test
