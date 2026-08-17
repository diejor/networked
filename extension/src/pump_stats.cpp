#include "netw/pump_stats.hpp"

#include <algorithm>

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

void NetwPumpStats::reset() {
    runtimes = 0;
    starving = 0;
    sleeping = 0;
    projecting = 0;
    snaps = 0;
    max_display_lag = 0.0;
    max_forecast_age = 0.0;
}

void NetwPumpStats::merge(const Ref<NetwPumpStats> &other) {
    NETW_ERR_COND(
        other.is_null(),
        sys::INTERPOLATION,
        "NetwPumpStats.merge: there is no record to merge."
    );
    runtimes += other->runtimes;
    starving += other->starving;
    sleeping += other->sleeping;
    projecting += other->projecting;
    snaps += other->snaps;
    max_display_lag = std::max(max_display_lag, other->max_display_lag);
    max_forecast_age = std::max(max_forecast_age, other->max_forecast_age);
}

#define NETW_PUMP_COUNTER(m_type, m_name)                                      \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwPumpStats::get_##m_name                                           \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwPumpStats::set_##m_name                                           \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

void NetwPumpStats::_bind_methods() {
    ClassDB::bind_method(D_METHOD("reset"), &NetwPumpStats::reset);
    ClassDB::bind_method(D_METHOD("merge", "other"), &NetwPumpStats::merge);

    NETW_PUMP_COUNTER(Variant::INT, runtimes);
    NETW_PUMP_COUNTER(Variant::INT, starving);
    NETW_PUMP_COUNTER(Variant::INT, sleeping);
    NETW_PUMP_COUNTER(Variant::INT, projecting);
    NETW_PUMP_COUNTER(Variant::INT, snaps);
    NETW_PUMP_COUNTER(Variant::FLOAT, max_display_lag);
    NETW_PUMP_COUNTER(Variant::FLOAT, max_forecast_age);
}

} // namespace netw
