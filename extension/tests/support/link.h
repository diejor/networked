#pragma once

#include "netw/api/link_conditions.hpp"

namespace netw_test {

inline godot::Ref<netw::NetwLinkConditions> link(int64_t p_seed = 0) {
    godot::Ref<netw::NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->set_seed(p_seed);
    return conditions;
}

inline godot::Ref<netw::NetwLinkConditions> polls(
    int p_count,
    double p_period_ms = 1000.0 / 60.0
) {
    godot::Ref<netw::NetwLinkConditions> conditions = link();
    conditions->set_latency_ms(double(p_count) * p_period_ms);
    return conditions;
}

} // namespace netw_test
