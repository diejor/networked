#include "netw/api/lag_compensation_config.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwLagCompensationConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_max_future_action_ticks", "ticks"),
        &NetwLagCompensationConfig::set_max_future_action_ticks
    );
    ClassDB::bind_method(
        D_METHOD("get_max_future_action_ticks"),
        &NetwLagCompensationConfig::get_max_future_action_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "max_future_action_ticks",
            PROPERTY_HINT_NONE,
            "suffix:ticks"
        ),
        "set_max_future_action_ticks",
        "get_max_future_action_ticks"
    );

    ClassDB::bind_method(
        D_METHOD("set_input_gate_deadline_ticks", "ticks"),
        &NetwLagCompensationConfig::set_input_gate_deadline_ticks
    );
    ClassDB::bind_method(
        D_METHOD("get_input_gate_deadline_ticks"),
        &NetwLagCompensationConfig::get_input_gate_deadline_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "input_gate_deadline_ticks",
            PROPERTY_HINT_NONE,
            "suffix:ticks"
        ),
        "set_input_gate_deadline_ticks",
        "get_input_gate_deadline_ticks"
    );

    ClassDB::bind_method(
        D_METHOD("max_future_action_ticks", "ticks"),
        &NetwLagCompensationConfig::max_future_action_ticks
    );
    ClassDB::bind_method(
        D_METHOD("input_gate_deadline_ticks", "ticks"),
        &NetwLagCompensationConfig::input_gate_deadline_ticks
    );
}

Ref<NetwLagCompensationConfig> NetwLagCompensationConfig::
    max_future_action_ticks(int64_t p_ticks) {
    set_max_future_action_ticks(p_ticks);
    return Ref<NetwLagCompensationConfig>(this);
}

Ref<NetwLagCompensationConfig> NetwLagCompensationConfig::
    input_gate_deadline_ticks(int64_t p_ticks) {
    set_input_gate_deadline_ticks(p_ticks);
    return Ref<NetwLagCompensationConfig>(this);
}

void NetwLagCompensationConfig::copy_values_from(
    const NetwLagCompensationConfig &p_source
) {
    values = p_source.values;
}

} // namespace netw
