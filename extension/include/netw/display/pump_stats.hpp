#pragma once

#include <cstdint>

namespace netw::display {

struct PumpStats {
    int32_t runtimes = 0;
    int32_t starving = 0;
    int32_t sleeping = 0;
    int32_t projecting = 0;
    int32_t snaps = 0;
    double max_display_lag = 0.0;
    double max_forecast_age = 0.0;

    void reset();
    void merge(const PumpStats &other);
};

} // namespace netw::display
