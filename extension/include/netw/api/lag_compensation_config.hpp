#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "netw/api/config_draft.hpp"

namespace netw {

class NetwLagCompensationConfig : public godot::Resource {
    GDCLASS(NetwLagCompensationConfig, godot::Resource)

    struct Values {
        int64_t max_future_action_ticks = 8;
        int64_t input_gate_deadline_ticks = 12;
    };

    Values values;
    config_draft::Guard guard{"configure_lagcomp"};

protected:
    static void _bind_methods();

public:
    void set_max_future_action_ticks(int64_t p_ticks) {
        if (guard.takes_count("max_future_action_ticks", p_ticks)) {
            values.max_future_action_ticks = p_ticks;
        }
    }
    int64_t get_max_future_action_ticks() const {
        return values.max_future_action_ticks;
    }

    void set_input_gate_deadline_ticks(int64_t p_ticks) {
        if (guard.takes_count("input_gate_deadline_ticks", p_ticks)) {
            values.input_gate_deadline_ticks = p_ticks;
        }
    }
    int64_t get_input_gate_deadline_ticks() const {
        return values.input_gate_deadline_ticks;
    }

    godot::Ref<NetwLagCompensationConfig> max_future_action_ticks(
        int64_t p_ticks
    );
    godot::Ref<NetwLagCompensationConfig> input_gate_deadline_ticks(
        int64_t p_ticks
    );

    void seal(const godot::String &p_scope) {
        guard.seal(p_scope);
    }
    void copy_values_from(const NetwLagCompensationConfig &p_source);
};

} // namespace netw
