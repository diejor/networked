#pragma once

/* A treeless value carrier for a declared entity.
 *
 * The sync and prediction bindings read named properties through Object, so a
 * declared session needs identity and values but no scene topology. Built-in
 * Node2D properties remain available, and any other declared field lives in
 * the dynamic property bag.
 */

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

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
    double rewound_x = 0.0;
    int rewind_visits = 0;
    bool count_replay_effects = false;
    double last_simulated_x = 0.0;

protected:
    static void _bind_methods() {
        godot::ClassDB::bind_method(
            godot::D_METHOD("_network_tick", "delta", "tick", "fresh"),
            &Carrier::network_tick
        );
        godot::ClassDB::bind_method(
            godot::D_METHOD("observe_self"),
            &Carrier::observe_self
        );
        godot::ClassDB::bind_method(
            godot::D_METHOD("sum_into_speed", "delta", "tick", "fresh"),
            &Carrier::sum_into_speed
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

    // Reads its own live position, which is what a rewind body is for: inside
    // the callable the node stands where history says it stood.
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

    // A step with no body of its own: it reads the input the port just
    // applied and writes the state the port is about to capture.
    void sum_into_speed(double, int64_t, bool) {
        set("speed", double(get("speed")) + double(get("throttle")));
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
