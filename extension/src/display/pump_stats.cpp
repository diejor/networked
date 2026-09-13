#include "netw/display/pump_stats.hpp"

#include <algorithm>

namespace netw::display {

void PumpStats::reset() {
    *this = PumpStats();
}

void PumpStats::merge(const PumpStats &other) {
    runtimes += other.runtimes;
    starving += other.starving;
    sleeping += other.sleeping;
    projecting += other.projecting;
    snaps += other.snaps;
    max_display_lag = std::max(max_display_lag, other.max_display_lag);
    max_forecast_age = std::max(max_forecast_age, other.max_forecast_age);
}

} // namespace netw::display
